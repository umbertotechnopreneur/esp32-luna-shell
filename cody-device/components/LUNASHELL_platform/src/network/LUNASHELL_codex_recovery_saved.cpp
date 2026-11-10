/* Copyright (c) 2026 Umberto Giacobbi */
#include "LUNASHELL_codex_recovery_saved.hpp"

#include "LUNASHELL_codex_state_saved.hpp"
#include "LUNASHELL_settings.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>

#include "esp_log.h"
#include "nvs.h"

namespace LUNASHELL::codex_recovery {
namespace {

using CODY::Domain::ChatId;

constexpr char kTag[] = "CodyRecovery";
constexpr char kKey[] = "ccp_resume_v1";
constexpr char kMacDomain[] = "cody.recovery.checkpoint/1";
constexpr uint64_t kDelayUs = 2'000'000;
constexpr size_t kHeaderBytes = 52;
constexpr size_t kAnswerBytes = 30;
constexpr size_t kPayloadBytes = kHeaderBytes + kCodexRecoveredAnswers * kAnswerBytes;
using Record = std::array<uint8_t, kPayloadBytes + 32>;
static_assert(kPayloadBytes <= 256, "Checkpoint authentication accepts at most 256 payload bytes");

struct Saved {
    ChatId bookmark{};
    CodexSavedTicket last_open{};
    std::array<CodexSavedAnswer, kCodexRecoveredAnswers> attempts{};
};

std::mutex s_mutex;
Saved s_queued{};
Saved s_durable{};
esp_err_t s_error = ESP_OK;
uint64_t s_due_us = 0;
uint64_t s_clock_us = 0;
bool s_writable = false;

// ticket: Raw 128-bit CCP1 identity; all zero means absence.
bool present(const CodexSavedTicket &ticket)
{
    return std::any_of(ticket.bytes.begin(), ticket.bytes.end(), [](uint8_t byte) { return byte != 0; });
}

// text: Null-terminated exact 32-digit lowercase hexadecimal request identity.
// ticket: Receives the bounded raw reference only on success.
bool decode_ticket(const char *text, CodexSavedTicket &ticket)
{
    if (!text || strnlen(text, 33) != 32) return false;
    CodexSavedTicket candidate{};
    for (size_t i = 0; i < 32; ++i) {
        const auto byte = text[i];
        unsigned digit;
        if (byte >= '0' && byte <= '9') digit = byte - '0';
        else if (byte >= 'a' && byte <= 'f') digit = byte - 'a' + 10;
        else return false;
        auto &value = candidate.bytes[i / 2];
        value = static_cast<uint8_t>((value << 4U) | digit);
    }
    if (!present(candidate)) return false;
    ticket = candidate;
    return true;
}

// choice: Validated canonical choice with zero-filled tail.
bool canonical_choice(const std::array<char, 13> &choice)
{
    const auto length = strnlen(choice.data(), choice.size());
    return length < choice.size() && validCodexChoiceId(choice.data()) &&
        std::all_of(choice.begin() + length, choice.end(), [](char value) { return value == 0; });
}

// left: One pending or verified recovery record.
// right: The other version of the same pairing's record.
bool same(const Saved &left, const Saved &right)
{
    if (left.bookmark != right.bookmark || left.last_open.bytes != right.last_open.bytes) return false;
    for (size_t i = 0; i < left.attempts.size(); ++i) {
        const auto &a = left.attempts[i];
        const auto &b = right.attempts[i];
        if (a.ticket.bytes != b.ticket.bytes || a.choice != b.choice ||
            a.broker_receipt_observed != b.broker_receipt_observed) return false;
    }
    return true;
}

// bytes: Destination of eight big-endian bytes.
// value: Stable source or chat token to serialize.
void encode_id(uint8_t *bytes, uint64_t value)
{
    for (size_t i = 8; i != 0; --i) {
        bytes[i - 1] = static_cast<uint8_t>(value);
        value >>= 8U;
    }
}

// bytes: Source of exactly eight big-endian bytes.
uint64_t decode_id(const uint8_t *bytes)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) value = (value << 8U) | bytes[i];
    return value;
}

// data: Record decoded only after version, association and MAC admission.
// record: Exact initialized byte array.
bool decode_payload(Saved &data, const Record &record)
{
    data.bookmark = {{decode_id(record.data() + 20)}, decode_id(record.data() + 28)};
    if (data.bookmark != ChatId{} && !data.bookmark.valid()) return false;
    std::copy_n(record.data() + 36, data.last_open.bytes.size(), data.last_open.bytes.begin());
    for (size_t i = 0; i < data.attempts.size(); ++i) {
        auto &answer = data.attempts[i];
        const auto *bytes = record.data() + kHeaderBytes + i * kAnswerBytes;
        std::copy_n(bytes, answer.ticket.bytes.size(), answer.ticket.bytes.begin());
        std::memcpy(answer.choice.data(), bytes + 16, answer.choice.size());
        if (bytes[29] > 1) return false;
        answer.broker_receipt_observed = bytes[29] == 1;
        if (!present(answer.ticket)) {
            if (answer.choice != std::array<char, 13>{} || answer.broker_receipt_observed) return false;
            continue;
        }
        if (!canonical_choice(answer.choice)) return false;
        for (size_t j = 0; j < i; ++j) {
            if (data.attempts[j].ticket.bytes == answer.ticket.bytes) return false;
        }
    }
    return true;
}

// data: Prepared snapshot of the in-memory owner record.
// peer: Active retained PC ID.
// record: Exact bounded checkpoint payload with no borrowed references.
void encode_payload(const Saved &data, const uint8_t *peer, Record &record)
{
    std::memcpy(record.data(), "CRV1", 4);
    std::memcpy(record.data() + 4, peer, 16);
    encode_id(record.data() + 20, data.bookmark.source.value);
    encode_id(record.data() + 28, data.bookmark.value);
    std::copy(data.last_open.bytes.begin(), data.last_open.bytes.end(), record.begin() + 36);
    for (size_t i = 0; i < data.attempts.size(); ++i) {
        const auto &answer = data.attempts[i];
        auto *bytes = record.data() + kHeaderBytes + i * kAnswerBytes;
        std::copy(answer.ticket.bytes.begin(), answer.ticket.bytes.end(), bytes);
        std::memcpy(bytes + 16, answer.choice.data(), answer.choice.size());
        bytes[29] = answer.broker_receipt_observed ? 1 : 0;
    }
}

// data: Owner state to search. Const qualification is preserved in the result.
// ticket: Exact question request identity; all-zero never reaches this helper.
template <typename Data>
auto find_answer(Data &data, const CodexSavedTicket &ticket)
{
    return std::find_if(data.attempts.begin(), data.attempts.end(), [&](const auto &answer) {
        return answer.ticket.bytes == ticket.bytes;
    });
}

// now_us: Monotonic admission time under the recovery mutex.
// urgent: An explicitly admitted answer must reach durable storage before sending.
void schedule(uint64_t now_us, bool urgent)
{
    if (urgent) s_due_us = now_us;
    else if (s_due_us == 0 || now_us < s_clock_us) s_due_us = now_us + kDelayUs;
    s_clock_us = now_us;
}

// No parameters: return the exact stored fault when no writes can be admitted.
esp_err_t write_error()
{
    return s_error == ESP_OK ? ESP_ERR_INVALID_STATE : s_error;
}

} // namespace

// peer: Current retained PC ID or null with key before pairing.
// key: Retained key used to verify association and complete contents.
void load(const uint8_t *peer, const uint8_t *key)
{
    Saved data{};
    Record record{};
    auto error = ESP_ERR_NVS_NOT_FOUND;
    if (peer && key) {
        error = UserSettingsService::instance().getBlobExact(kKey, record.data(), record.size());
        if (error == ESP_OK && std::memcmp(record.data(), "CRV1", 4) != 0) error = ESP_ERR_INVALID_VERSION;
        if (error == ESP_OK && std::memcmp(record.data() + 4, peer, 16) != 0) error = ESP_ERR_INVALID_STATE;
        if (error == ESP_OK) {
            error = codex_state_saved::authenticate_checkpoint(record.data(), kPayloadBytes, kMacDomain, key, true);
        }
        if (error == ESP_OK && !decode_payload(data, record)) error = ESP_ERR_INVALID_RESPONSE;
    }
    std::lock_guard<std::mutex> guard(s_mutex);
    s_durable = error == ESP_OK ? data : Saved{};
    s_queued = s_durable;
    s_due_us = 0;
    s_clock_us = 0;
    const bool missing = error == ESP_ERR_NVS_NOT_FOUND;
    s_writable = peer && key && (missing || error == ESP_OK);
    s_error = missing ? ESP_OK : error;
    if (s_error != ESP_OK) ESP_LOGE(kTag, "Recovery checkpoint restore failed (%s)", esp_err_to_name(s_error));
}

// No parameters: confirm the old association's exact checkpoint is absent.
esp_err_t erase()
{
    auto &settings = UserSettingsService::instance();
    auto error = settings.eraseKey(kKey);
    if (error == ESP_OK || error == ESP_ERR_NVS_NOT_FOUND) {
        Record check{};
        const auto readback = settings.getBlobExact(kKey, check.data(), check.size());
        error = readback == ESP_ERR_NVS_NOT_FOUND ? ESP_OK :
            readback == ESP_OK ? ESP_ERR_INVALID_RESPONSE : readback;
    }
    std::lock_guard<std::mutex> guard(s_mutex);
    s_writable = error == ESP_OK;
    s_error = error;
    if (error == ESP_OK) {
        s_queued = {};
        s_durable = {};
        s_due_us = 0;
        s_clock_us = 0;
    } else {
        ESP_LOGE(kTag, "Recovery checkpoint removal failed (%s)", esp_err_to_name(error));
    }
    return error;
}

// peer: Active retained association owned by this worker.
// key: Corresponding retained link key.
// now_us: Monotonic pacing time, never used as a ticket deadline.
void checkpoint(const uint8_t *peer, const uint8_t *key, uint64_t now_us)
{
    Saved pending{};
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (!s_writable || same(s_queued, s_durable)) return;
        if (now_us < s_clock_us) s_due_us = now_us + kDelayUs;
        s_clock_us = now_us;
        if (now_us < s_due_us) return;
        pending = s_queued;
    }

    Record record{};
    encode_payload(pending, peer, record);
    auto error = codex_state_saved::authenticate_checkpoint(record.data(), kPayloadBytes, kMacDomain, key, false);
    auto &settings = UserSettingsService::instance();
    if (error == ESP_OK) error = settings.setBlob(kKey, record.data(), record.size());
    bool readback_failed = false;
    if (error == ESP_OK) {
        Record readback{};
        error = settings.getBlobExact(kKey, readback.data(), readback.size());
        if (error == ESP_OK && readback != record) error = ESP_ERR_INVALID_RESPONSE;
        readback_failed = error != ESP_OK;
    }

    std::lock_guard<std::mutex> guard(s_mutex);
    s_error = error;
    if (error == ESP_OK) {
        s_durable = pending;
        // A concurrent UI request may have queued another value while settings
        // was committing. Keep its contents and deadline intact.
        if (same(s_queued, pending)) s_due_us = 0;
    } else {
        s_due_us = now_us + kDelayUs;
        if (readback_failed) s_writable = false;
        ESP_LOGE(kTag, "Recovery checkpoint commit/readback failed (%s)", esp_err_to_name(error));
    }
}

// chat: Validated exact live bookmark, or explicit zero for removal.
// now_us: Monotonic owner time used to coalesce selection writes.
esp_err_t queue_selection(const ChatId &chat, uint64_t now_us)
{
    if (chat != ChatId{} && !chat.valid()) return ESP_ERR_INVALID_ARG;
    std::unique_lock<std::mutex> guard(s_mutex, std::try_to_lock);
    if (!guard.owns_lock()) return ESP_ERR_TIMEOUT;
    if (!s_writable) return write_error();
    if (s_queued.bookmark != chat) {
        s_queued.bookmark = chat;
        schedule(now_us, false);
    }
    return ESP_OK;
}

// chat: Verified durable bookmark, unchanged on errors or pending change.
esp_err_t read_selection(ChatId &chat)
{
    std::unique_lock<std::mutex> guard(s_mutex, std::try_to_lock);
    if (!guard.owns_lock()) return ESP_ERR_TIMEOUT;
    if (s_error != ESP_OK) return s_error;
    if (s_queued.bookmark != s_durable.bookmark) return ESP_ERR_NOT_FINISHED;
    if (!s_durable.bookmark.valid()) return ESP_ERR_NOT_FOUND;
    chat = s_durable.bookmark;
    return ESP_OK;
}

// result: Bounded historical observation of durable unresolved references.
esp_err_t read_recovery(CodexRecoveryObservation &result)
{
    std::unique_lock<std::mutex> guard(s_mutex, std::try_to_lock);
    if (!guard.owns_lock()) return ESP_ERR_TIMEOUT;
    result = {};
    result.storage_error = s_error;
    result.last_open = s_durable.last_open;
    for (const auto &attempt : s_durable.attempts) {
        if (present(attempt.ticket)) result.attempts[result.attempt_count++] = attempt;
    }
    if (s_error != ESP_OK) return s_error;
    return present(result.last_open) || result.attempt_count != 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

// request: Authenticated exact CCP1 question ticket, never a native approval.
// choice: Exact observed choice when a broker reports receipt or delivery.
// state: Broker observation; local expiry and unavailable are not closure.
// now_us: Monotonic save pacing time.
void observe(const char *request, const char *choice, CodexEventState state, uint64_t now_us)
{
    CodexSavedTicket ticket{};
    if (!decode_ticket(request, ticket)) return;
    std::lock_guard<std::mutex> guard(s_mutex);
    if (!s_writable) return;
    auto found = find_answer(s_queued, ticket);
    bool changed = false;
    if (state == CodexEventState::Pending || state == CodexEventState::Received) {
        if (s_queued.last_open.bytes != ticket.bytes) {
            s_queued.last_open = ticket;
            changed = true;
        }
        if (state == CodexEventState::Received && found != s_queued.attempts.end() &&
            !found->broker_receipt_observed && choice && std::strcmp(found->choice.data(), choice) == 0) {
            found->broker_receipt_observed = true;
            changed = true;
        }
    } else if (state == CodexEventState::Cancelled || state == CodexEventState::Delivered) {
        const bool exact = found == s_queued.attempts.end() || state == CodexEventState::Cancelled ||
            (choice && std::strcmp(found->choice.data(), choice) == 0);
        if (!exact) return;
        if (found != s_queued.attempts.end()) {
            *found = {};
            changed = true;
        }
        if (s_queued.last_open.bytes == ticket.bytes) {
            s_queued.last_open = {};
            changed = true;
        }
    }
    if (changed) schedule(now_us, false);
}

// request: Exact currently authenticated question ticket.
// choice: Explicitly confirmed immutable choice.
// now_us: Admission clock for the immediate write-ahead checkpoint.
esp_err_t queue_answer(const char *request, const char *choice, uint64_t now_us)
{
    CodexSavedTicket ticket{};
    if (!decode_ticket(request, ticket) || !validCodexChoiceId(choice)) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> guard(s_mutex);
    if (!s_writable) return write_error();
    // Even an identical restored answer is uncertain. Only a new authenticated
    // outcome can retire it; this path never resurrects its send permission.
    if (find_answer(s_queued, ticket) != s_queued.attempts.end() ||
        find_answer(s_durable, ticket) != s_durable.attempts.end()) return ESP_ERR_INVALID_STATE;
    auto slot = std::find_if(s_queued.attempts.begin(), s_queued.attempts.end(),
        [](const auto &attempt) { return !present(attempt.ticket); });
    if (slot == s_queued.attempts.end()) return ESP_ERR_NO_MEM;
    slot->ticket = ticket;
    std::memcpy(slot->choice.data(), choice, std::strlen(choice));
    s_queued.last_open = ticket;
    schedule(now_us, true);
    return ESP_OK;
}

// request: Already confirmed request identity held by the current live transaction.
// choice: Exact selected choice for the same transaction.
esp_err_t answer_ready(const char *request, const char *choice)
{
    CodexSavedTicket ticket{};
    if (!decode_ticket(request, ticket) || !validCodexChoiceId(choice)) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> guard(s_mutex);
    const auto staged = find_answer(s_queued, ticket);
    if (staged == s_queued.attempts.end() || std::strcmp(staged->choice.data(), choice) != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    const auto saved = find_answer(s_durable, ticket);
    if (saved != s_durable.attempts.end() && std::strcmp(saved->choice.data(), choice) == 0) return ESP_OK;
    return s_error != ESP_OK ? s_error : ESP_ERR_NOT_FINISHED;
}

} // namespace LUNASHELL::codex_recovery
