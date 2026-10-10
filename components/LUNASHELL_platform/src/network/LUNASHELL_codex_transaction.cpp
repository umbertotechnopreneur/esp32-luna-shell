#include "LUNASHELL_codex_transaction.hpp"

#include <cstring>
#include <algorithm>
#include <limits>

namespace LUNASHELL {
namespace {

// value: Exact bounded identity, including its terminating NUL.
// zero: Receives whether this is the protocol's empty identity.
bool validIdentity(const char *value, bool &zero)
{
    if (value == nullptr) {
        return false;
    }
    zero = true;
    for (size_t index = 0; index < 32; ++index) {
        const char digit = value[index];
        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'))) {
            return false;
        }
        zero = zero && digit == '0';
    }
    return value[32] == '\0';
}

// left: Previously admitted immutable content.
// right: Newly decoded content; selection is the only mutable payload field.
bool sameContent(const CodexEventPayload &left, const CodexEventPayload &right)
{
    if (left.kind != right.kind || std::strcmp(left.context, right.context) != 0 ||
        std::strcmp(left.title, right.title) != 0 || std::strcmp(left.body, right.body) != 0 ||
        left.choice_count != right.choice_count || left.failure_count != right.failure_count ||
        left.progress_valid != right.progress_valid || left.passed != right.passed || left.total != right.total) {
        return false;
    }
    for (uint8_t index = 0; index < left.choice_count; ++index) {
        const auto &a = left.choices[index];
        const auto &b = right.choices[index];
        if (std::strcmp(a.id, b.id) != 0 || std::strcmp(a.label, b.label) != 0 ||
            std::strcmp(a.detail, b.detail) != 0 || a.tone != b.tone) {
            return false;
        }
    }
    for (uint8_t index = 0; index < left.failure_count; ++index) {
        if (std::strcmp(left.failed_checks[index], right.failed_checks[index]) != 0) {
            return false;
        }
    }
    return true;
}
} // namespace

// token: Exact EVENT wire token; socket authentication belongs to the transport owner.
// state: Receives a supported state, with Unavailable mapped to local Rejected.
bool decodeCodexEventState(const char *token, CodexEventState &state)
{
    if (token == nullptr) {
        return false;
    }
    struct Entry { const char *token; CodexEventState state; };
    static constexpr Entry entries[] = {
        {"none", CodexEventState::None}, {"pending", CodexEventState::Pending},
        {"received", CodexEventState::Received}, {"delivered", CodexEventState::Delivered},
        {"cancelled", CodexEventState::Cancelled}, {"expired", CodexEventState::Expired},
        {"unavailable", CodexEventState::Rejected},
    };
    for (const auto &entry : entries) {
        if (std::strcmp(token, entry.token) == 0) {
            state = entry.state;
            return true;
        }
    }
    return false;
}

// current: Exclusive owner-held live transaction.
// request_id: Authenticated immutable request identity.
// state: Validated wire state, with Rejected reserved for Unavailable.
// ttl: Bounded lifetime in seconds.
// candidate: Valid decoded payload, or nullptr for absence/retirement.
// now_us: Owner's monotonic receive time.
// effect: Publication and transport effects applied by the owner after success.
// Exception: Returns original admission error without mutation.
esp_err_t applyCodexEvent(CodexTransactionView current, const char *request_id,
    CodexEventState state, uint16_t ttl, const CodexEventPayload *candidate,
    int64_t now_us, CodexTransactionEffect &effect)
{
    effect = {};
    bool zero = false;
    if (!validIdentity(request_id, zero) || ttl > 180 || now_us < 0 ||
        now_us > std::numeric_limits<int64_t>::max() - static_cast<int64_t>(ttl) * 1000000LL) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool same = std::strcmp(request_id, current.request_id) == 0;
    if (state == CodexEventState::None || state == CodexEventState::Rejected) {
        if (ttl != 0 || candidate != nullptr || (state == CodexEventState::None && !zero)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (state == CodexEventState::Rejected && (zero || !same || current.payload.kind != CodexEventKind::Question)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (current.payload.kind != CodexEventKind::Question && state == CodexEventState::None) {
            effect.changed = current.state != CodexEventState::None;
            current.payload = {};
            current.state = CodexEventState::None;
            current.last_error = ESP_OK;
        } else if (codexEventIsOpen(current.state)) {
            current.state = CodexEventState::Rejected;
            current.last_error = ESP_ERR_NOT_FOUND;
            effect.changed = true;
            effect.unavailable = true;
        }
        effect.clear_reply = effect.changed;
        return ESP_OK;
    }
    if (zero || candidate == nullptr ||
        (state != CodexEventState::Pending && state != CodexEventState::Received &&
         state != CodexEventState::Delivered && state != CodexEventState::Cancelled && state != CodexEventState::Expired) ||
        (codexEventIsOpen(state) && ttl == 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool answered = state == CodexEventState::Received || state == CodexEventState::Delivered;
    if ((answered && (candidate->kind != CodexEventKind::Question || candidate->selected_id[0] == '\0')) ||
        (state == CodexEventState::Pending && candidate->selected_id[0] != '\0')) {
        return ESP_ERR_INVALID_ARG;
    }
    if (same && !sameContent(current.payload, *candidate)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (!same && current.payload.kind == CodexEventKind::Question && codexEventIsOpen(current.state)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (same && codexEventIsTerminal(current.state) && state != current.state) {
        if (!codexEventIsTerminal(state)) {
            return ESP_ERR_INVALID_STATE;
        }
        effect.acknowledge = current.payload.kind == CodexEventKind::Question;
        return ESP_OK;
    }
    if (same && current.payload.selected_id[0] != '\0' && answered &&
        std::strcmp(current.payload.selected_id, candidate->selected_id) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const bool keep_reply = same && state == CodexEventState::Pending &&
        (current.state == CodexEventState::Sending || current.state == CodexEventState::Received);
    const bool elapsed = same && codexEventIsOpen(current.state) && now_us >= current.expires_at_us;
    const int64_t deadline = now_us + static_cast<int64_t>(ttl) * 1000000LL;
    effect.changed = !same || (!keep_reply && state != current.state);
    effect.wake = !same && state == CodexEventState::Pending;
    if (!same) {
        current.payload = *candidate;
        std::memcpy(current.request_id, request_id, 33);
        current.expires_at_us = deadline;
    } else {
        if (answered) {
            std::memcpy(current.payload.selected_id, candidate->selected_id, sizeof(current.payload.selected_id));
        }
        current.expires_at_us = std::min(current.expires_at_us, deadline);
    }
    if (!keep_reply) {
        current.state = state;
    }
    if (elapsed || (codexEventIsOpen(current.state) && now_us >= current.expires_at_us)) {
        current.state = CodexEventState::Expired;
        effect.changed = true;
    }
    current.last_error = current.state == CodexEventState::Expired ? ESP_ERR_TIMEOUT : ESP_OK;
    effect.clear_reply = !same || !codexEventIsOpen(current.state) || answered;
    effect.acknowledge = current.payload.kind == CodexEventKind::Question && codexEventIsTerminal(current.state);
    return ESP_OK;
}

// current: Owner-held current transaction.
// request_id: Authenticated acknowledgement identity.
// choice_id: Authenticated semantic choice.
// status: Exact wire status, never inferred from socket delivery.
// now_us: Owner's monotonic receive time.
// effect: Reports admitted mutation and actual terminal observation.
// Exception: Returns admission failure, or recipient error after an admitted mutation.
esp_err_t applyCodexReplyAck(CodexTransactionView current, const char *request_id,
    const char *choice_id, const char *status, int64_t now_us, CodexTransactionEffect &effect)
{
    effect = {};
    if (request_id == nullptr || choice_id == nullptr || status == nullptr || now_us < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (std::strcmp(request_id, current.request_id) != 0 || std::strcmp(choice_id, current.payload.selected_id) != 0 ||
        (current.state != CodexEventState::Sending && current.state != CodexEventState::Received && current.state != CodexEventState::Delivered)) {
        return ESP_ERR_INVALID_STATE;
    }
    CodexEventState next;
    esp_err_t error = ESP_OK;
    if (std::strcmp(status, "received") == 0) {
        next = CodexEventState::Received;
    } else if (std::strcmp(status, "delivered") == 0) {
        next = CodexEventState::Delivered;
    } else if (std::strcmp(status, "expired") == 0) {
        next = CodexEventState::Expired;
        error = ESP_ERR_TIMEOUT;
    } else if (std::strcmp(status, "cancelled") == 0) {
        next = CodexEventState::Cancelled;
        error = ESP_ERR_INVALID_STATE;
    } else if (std::strcmp(status, "conflict") == 0 || std::strcmp(status, "unknown") == 0) {
        next = CodexEventState::Rejected;
        error = ESP_ERR_INVALID_RESPONSE;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    if (current.state == CodexEventState::Delivered && next != CodexEventState::Delivered) {
        return ESP_ERR_INVALID_STATE;
    }
    if (codexEventIsOpen(current.state) && now_us >= current.expires_at_us) {
        next = CodexEventState::Expired;
        error = ESP_ERR_TIMEOUT;
    }
    effect.changed = next != current.state || error != current.last_error;
    current.state = next;
    current.last_error = error;
    effect.clear_reply = true;
    effect.acknowledge = codexEventIsTerminal(next);
    return error;
}

// current: Exclusive owner-held transaction.
// now_us: Explicit monotonic maintenance time.
// effect: Mutation and pending-send withdrawal decisions.
void expireCodexTransaction(CodexTransactionView current, int64_t now_us, CodexTransactionEffect &effect)
{
    effect = {};
    if (!codexEventIsOpen(current.state) || now_us < current.expires_at_us) {
        return;
    }
    if (current.payload.kind == CodexEventKind::Question) {
        current.state = CodexEventState::Expired;
        current.last_error = ESP_ERR_TIMEOUT;
    } else {
        current.payload = {};
        current.state = CodexEventState::None;
        current.last_error = ESP_OK;
    }
    effect.changed = true;
    effect.clear_reply = true;
}

// current: Owner-held question.
// request_id: Identity displayed at confirmation.
// choice_id: Stable authored choice identity.
// fresh: Owner-confirmed connection and canonical freshness.
// now_us: Explicit monotonic Send time.
// Exception: Returns precise admission failure without selecting or queuing a reply.
esp_err_t submitCodexChoice(CodexTransactionView current, const char *request_id,
    const char *choice_id, bool fresh, int64_t now_us)
{
    if (request_id == nullptr || !validCodexChoiceId(choice_id) || now_us < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!fresh || current.payload.kind != CodexEventKind::Question || current.state != CodexEventState::Pending ||
        std::strcmp(request_id, current.request_id) != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (now_us >= current.expires_at_us) {
        return ESP_ERR_TIMEOUT;
    }
    bool found = false;
    for (uint8_t index = 0; index < current.payload.choice_count; ++index) {
        found = found || std::strcmp(choice_id, current.payload.choices[index].id) == 0;
    }
    if (!found) {
        return ESP_ERR_INVALID_ARG;
    }
    std::memcpy(current.payload.selected_id, choice_id, std::strlen(choice_id) + 1);
    current.state = CodexEventState::Sending;
    current.last_error = ESP_OK;
    return ESP_OK;
}

// current: Owner-held transaction; owner retains a rollback buffer if publication can fail.
// request_id: Exact identity explicitly dismissed.
// Exception: Returns INVALID_STATE without mutation for an open question or changed identity.
esp_err_t dismissCodexTransaction(CodexTransactionView current, const char *request_id)
{
    if (request_id == nullptr || std::strcmp(request_id, current.request_id) != 0 || current.state == CodexEventState::None ||
        (current.payload.kind == CodexEventKind::Question && codexEventIsOpen(current.state))) {
        return ESP_ERR_INVALID_STATE;
    }
    current.payload = {};
    current.state = CodexEventState::None;
    current.last_error = ESP_OK;
    return ESP_OK;
}
} // namespace LUNASHELL
