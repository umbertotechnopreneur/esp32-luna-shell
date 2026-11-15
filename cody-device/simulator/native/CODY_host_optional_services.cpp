// Optional host boundaries for services that are not attached to a Cody device.
#include "LUNASHELL_codex_debug.hpp"
#include "LUNASHELL_build_metadata.hpp"
#include "CODY_host_studio_statistics.hpp"
#include "LUNASHELL_snapshot_aging.hpp"
#include "LUNASHELL_snapshot_internal.hpp"
#include "esp_timer.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <mutex>
#include <new>

namespace CODY::host_studio_statistics {
namespace {
enum class Page { None, Today, SevenDays, TokenHistory, Credits, Account, Context };

struct FixtureOwner {
    std::mutex mutex;
    LUNASHELL::CodexAccountSnapshot account{};
    LUNASHELL::CodexTokenIntervalsSnapshot intervals{};
    Page page = Page::None;
    int64_t context_published_at_us = 0;
};
// Host workers have process lifetime; their service observations have the same lifetime.
FixtureOwner *const s_owner = new (std::nothrow) FixtureOwner{};
constexpr char kContextChat[] = "2222222222222222222222222222222222222222222222222222222222222222";
constexpr char kContextTurn[] = "3333333333333333333333333333333333333333333333333333333333333333";

// target: Fixed-capacity ABI identity, independent of firmware enum ordinals.
// Exception: No exceptions; unknown targets never select a statistics fixture.
Page page_for(const char (&target)[48])
{
    struct Entry { const char *target; Page page; };
    constexpr Entry entries[]{
        {"cody.dashboard.today", Page::Today}, {"cody.dashboard.seven-days", Page::SevenDays},
        {"cody.dashboard.token-history", Page::TokenHistory}, {"cody.dashboard.credits", Page::Credits},
        {"cody.dashboard.account", Page::Account}, {"cody.dashboard.context", Page::Context},
        {"cody.activity.current-turn", Page::Context}};
    for (const auto &entry : entries)
        if (std::strncmp(target, entry.target, sizeof(target)) == 0) return entry.page;
    return Page::None;
}

// values: Complete bounded input group, including string bytes after its terminator.
// Exception: No exceptions; false rejects any supplied value in an unrelated group.
template<class Value, size_t Count> bool empty(const Value (&values)[Count])
{
    return std::all_of(std::begin(values), std::end(values), [](Value value) { return value == 0; });
}

// text: Bounded printable ASCII field, including its terminating NUL.
// allow_empty: Whether an intentionally absent optional caption is admitted.
// Exception: No exceptions; unterminated and control-character inputs are rejected.
template<size_t Count> bool ascii(const char (&text)[Count], bool allow_empty = true)
{
    const auto *end = std::find(text, text + Count, '\0');
    return end != text + Count && (allow_empty || end != text) &&
        std::all_of(text, end, [](char value) { return value >= 32 && value <= 126; });
}

// clock: Receives the canonical civil-time observation for UTC bucket identities.
// Exception: Preserves unavailable observations and rejects stale, invalid or out-of-range dates.
esp_err_t read_clock(LUNASHELL::ClockSnapshot &clock)
{
    const auto error = LUNASHELL::DeviceSnapshotService::instance().readClockObservation(clock);
    if (error != ESP_OK) return error;
    if (!clock.data_valid || clock.data_stale || clock.last_error != ESP_OK ||
        clock.utc_epoch_seconds < 96 * 900 ||
        clock.utc_epoch_seconds / 900 > std::numeric_limits<uint32_t>::max()) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

// now: Monotonic observation time, never the PC civil clock.
// ready: Receives the actual canonical synthetic READY/app lease admission.
// Exception: Preserves the owner read failure without qualifying a partial observation.
esp_err_t current_lease(int64_t now, bool &ready)
{
    if (now <= 0) return ESP_ERR_INVALID_STATE;
    LUNASHELL::CodexPresenceStatus status{};
    const auto error = LUNASHELL::DeviceSnapshotService::instance().readCodexPresenceStatus(status);
    if (error == ESP_OK) ready = status.codex_app_open;
    return error;
}

// request: Optional context input owned by the serialized Studio service preparation.
// previous_at: Receipt of this helper's preceding context publication, or zero.
// now: Current positive monotonic timestamp for a new observation.
// Exception: Preserves canonical publication errors; no physical or authenticated owner is contacted.
esp_err_t publish_context(const CodyHostStudioCase &request, int64_t previous_at, int64_t now)
{
    const bool context = page_for(request.identity.target) == Page::Context;
    if (!context && previous_at == 0) return ESP_OK;
    const auto mutation = [&](LUNASHELL::DeviceSnapshot &snapshot) {
        auto &technical = snapshot.codex_technical;
        auto &activity = snapshot.radios.codex_link.activity;
        // Retire only our own preceding observation. Other scenario sources are
        // never cleared merely because a statistics case stopped using them.
        if (previous_at > 0 && technical.received_at_us == previous_at &&
            std::strcmp(technical.chat_id, kContextChat) == 0) technical = {};
        if (previous_at > 0 && activity.last_success_at_us == previous_at &&
            std::strcmp(activity.chat_id, kContextChat) == 0) activity = {};
        if (context) {
            const auto &input = request.statistics;
            activity = {};
            LUNASHELL::snapshot_detail::mark_sample_success(activity, now);
            activity.state = LUNASHELL::CodexActivityState::Working;
            activity.revision = 1;
            std::memcpy(activity.chat_id, kContextChat, sizeof(kContextChat));
            std::memcpy(activity.title, input.context_chat_title, std::strlen(input.context_chat_title) + 1);
            activity.title_state = LUNASHELL::CodexChatTitleState::Current;
            activity.title_age_at_receive_us = static_cast<uint64_t>(input.source_age_ms) * 1000;
            activity.title_error = ESP_OK;
            technical = {};
            LUNASHELL::snapshot_detail::mark_sample_success(technical, now);
            std::memcpy(technical.chat_id, kContextChat, sizeof(kContextChat));
            std::memcpy(technical.turn_id, kContextTurn, sizeof(kContextTurn));
            technical.revision = 1;
            technical.received_at_us = now;
            technical.context_valid = technical.used_valid = technical.capacity_valid = true;
            technical.cumulative_valid = true;
            technical.used = input.context_used_tokens;
            technical.capacity = input.context_capacity_tokens;
            technical.cumulative = input.context_cumulative_tokens;
            technical.usage_age_at_receive_us = static_cast<uint64_t>(input.source_age_ms) * 1000;
            // No compaction, model, cost or effort fact is implied by counters.
            LUNASHELL::age_codex_technical(technical, now);
        }
        ++snapshot.sequence;
    };
    return LUNASHELL::snapshot_detail::update_snapshot(mutation);
}
} // namespace

// request: Explicit bounded counter input, with a stable textual target.
// Exception: Rejects unrelated payload, invalid fields, dates and unavailable clocks before mutation.
esp_err_t validate(const CodyHostStudioCase &request)
{
    const auto page = page_for(request.identity.target);
    const auto &input = request.statistics;
    const bool daily_empty = input.daily_valid_mask == 0 && empty(input.daily_tokens);
    const bool credits_empty = empty(input.credits_kind) && input.credit_milliunits == 0 && input.reset_vouchers == 0;
    const bool plan_empty = empty(input.plan) && empty(input.plan_label) && empty(input.plan_price);
    const bool intervals_empty = empty(input.interval_tokens) && empty(input.interval_valid_mask);
    const bool context_empty = input.context_used_tokens == 0 && input.context_capacity_tokens == 0 &&
        input.context_cumulative_tokens == 0 && empty(input.context_chat_title);
    const bool daily_page = page == Page::Today || page == Page::SevenDays;
    if ((!daily_page && !daily_empty) || (page != Page::Credits && !credits_empty) ||
        (page != Page::Account && !plan_empty) || (page != Page::TokenHistory && !intervals_empty) ||
        (page != Page::Context && !context_empty) || input.source_age_ms > 60000 ||
        (page == Page::None && input.source_age_ms != 0) || input.daily_valid_mask > 255 ||
        !ascii(input.credits_kind) || !ascii(input.plan) || !ascii(input.plan_label) ||
        !ascii(input.plan_price) || !ascii(input.context_chat_title)) return ESP_ERR_INVALID_ARG;
    if (page == Page::None) return ESP_OK;
    if (page == Page::Credits) {
        const bool finite = std::strcmp(input.credits_kind, "finite") == 0;
        if ((!finite && std::strcmp(input.credits_kind, "unlimited") != 0 &&
             std::strcmp(input.credits_kind, "unavailable") != 0) || input.credit_milliunits < 0 ||
            (!finite && input.credit_milliunits != 0)) return ESP_ERR_INVALID_ARG;
    }
    if (page == Page::Account && !ascii(input.plan, false)) return ESP_ERR_INVALID_ARG;
    if (page == Page::Context && (!ascii(input.context_chat_title, false) ||
        input.context_capacity_tokens == 0)) return ESP_ERR_INVALID_ARG;
    if (daily_page || page == Page::TokenHistory) {
        LUNASHELL::ClockSnapshot clock{};
        return read_clock(clock);
    }
    return ESP_OK;
}

// request: Validated case published after the canonical host link owner.
// Exception: Preserves validation, clock, allocation and snapshot publication failures.
esp_err_t publish(const CodyHostStudioCase &request)
{
    auto error = validate(request);
    if (error != ESP_OK) return error;
    if (!s_owner) return ESP_ERR_NO_MEM;
    std::lock_guard<std::mutex> lock(s_owner->mutex);
    const auto page = page_for(request.identity.target);
    const auto now = esp_timer_get_time();
    if (now <= 0) return ESP_ERR_INVALID_STATE;
    LUNASHELL::ClockSnapshot clock{};
    if (page == Page::Today || page == Page::SevenDays || page == Page::TokenHistory) {
        error = read_clock(clock);
        if (error != ESP_OK) return error;
    }
    if (page != Page::None) {
        bool ready = false;
        error = current_lease(now, ready);
        if (error != ESP_OK) return error;
        if (!ready) return ESP_ERR_INVALID_STATE;
    }
    error = publish_context(request, s_owner->context_published_at_us, now);
    if (error != ESP_OK) return error;
    s_owner->page = page;
    s_owner->account = {};
    s_owner->intervals = {};
    s_owner->context_published_at_us = page == Page::Context ? now : 0;
    const auto &input = request.statistics;
    if (page == Page::Today || page == Page::SevenDays || page == Page::Credits || page == Page::Account) {
        auto &account = s_owner->account;
        LUNASHELL::snapshot_detail::mark_sample_success(account, now);
        account.source_age_at_receive_ms = input.source_age_ms;
        if (page == Page::Today || page == Page::SevenDays) {
            account.anchor_unix_day = static_cast<uint32_t>(clock.utc_epoch_seconds / 86400);
            account.daily_valid_mask = static_cast<uint8_t>(input.daily_valid_mask);
            std::copy(std::begin(input.daily_tokens), std::end(input.daily_tokens), std::begin(account.daily_tokens));
        } else if (page == Page::Credits) {
            if (std::strcmp(input.credits_kind, "finite") == 0) account.credits_kind = LUNASHELL::CodexCreditKind::Finite;
            else if (std::strcmp(input.credits_kind, "unlimited") == 0) account.credits_kind = LUNASHELL::CodexCreditKind::Unlimited;
            account.credit_milliunits = static_cast<uint64_t>(input.credit_milliunits);
            account.reset_vouchers = input.reset_vouchers;
            account.reset_vouchers_valid = true;
        } else {
            std::memcpy(account.plan, input.plan, sizeof(account.plan));
            std::memcpy(account.plan_label, input.plan_label, sizeof(account.plan_label));
            std::memcpy(account.plan_price, input.plan_price, sizeof(account.plan_price));
            account.plan_projection_supported = true;
        }
    } else if (page == Page::TokenHistory) {
        auto &history = s_owner->intervals;
        LUNASHELL::snapshot_detail::mark_sample_success(history, now);
        history.source_age_at_receive_ms = input.source_age_ms;
        history.initialized = true;
        history.anchor_quarter = static_cast<uint32_t>(clock.utc_epoch_seconds / 900);
        history.first_observed_quarter = history.anchor_quarter;
        for (uint32_t index = 0; index < history.kCapacity; ++index) {
            const auto quarter = history.anchor_quarter - (history.kCapacity - 1 - index);
            const auto slot = quarter % history.kCapacity;
            if ((input.interval_valid_mask[index / 32] & (uint32_t{1} << (index % 32))) == 0) continue;
            history.tokens[slot] = input.interval_tokens[index];
            history.valid_mask[slot / 32] |= uint32_t{1} << (slot % 32);
            history.first_observed_quarter = std::min(history.first_observed_quarter, quarter);
        }
    }
    return ESP_OK;
}

// account: Receives a copied synthetic observation qualified by the canonical link lease.
// Exception: Returns NOT_SUPPORTED without modifying output outside account fixture cases.
esp_err_t read_account(LUNASHELL::CodexAccountSnapshot &account)
{
    if (!s_owner) return ESP_ERR_NO_MEM;
    std::lock_guard<std::mutex> lock(s_owner->mutex);
    if (s_owner->page != Page::Today && s_owner->page != Page::SevenDays &&
        s_owner->page != Page::Credits && s_owner->page != Page::Account) return ESP_ERR_NOT_SUPPORTED;
    const auto now = esp_timer_get_time();
    bool ready = false;
    const auto error = current_lease(now, ready);
    if (error != ESP_OK) return error;
    auto current = s_owner->account;
    LUNASHELL::age_codex_account_sample(current, current.source_age_at_receive_ms, ready, now);
    account = current;
    return ESP_OK;
}

// intervals: Receives the explicit synthetic UTC-quarter series and its actual source state.
// Exception: Returns NOT_SUPPORTED without modifying output outside the history fixture.
esp_err_t read_intervals(LUNASHELL::CodexTokenIntervalsSnapshot &intervals)
{
    if (!s_owner) return ESP_ERR_NO_MEM;
    std::lock_guard<std::mutex> lock(s_owner->mutex);
    if (s_owner->page != Page::TokenHistory) return ESP_ERR_NOT_SUPPORTED;
    const auto now = esp_timer_get_time();
    bool ready = false;
    const auto error = current_lease(now, ready);
    if (error != ESP_OK) return error;
    // Caller owns this larger bounded projection; do not add an 832-byte temporary
    // to the display owner's stack in addition to its retained history storage.
    intervals = s_owner->intervals;
    LUNASHELL::age_codex_account_sample(intervals, intervals.source_age_at_receive_ms, ready, now);
    return ESP_OK;
}

// vouchers: Remains unchanged because expiry observations are not part of this fixture contract.
// Exception: Returns NOT_SUPPORTED rather than inventing expiry dates.
esp_err_t read_vouchers(LUNASHELL::CodexVoucherSnapshot &vouchers)
{
    (void)vouchers;
    return ESP_ERR_NOT_SUPPORTED;
}
} // namespace CODY::host_studio_statistics

namespace LUNASHELL {

// request: Destination remains unchanged; no simulated authenticated mailbox exists.
// Exception: Returns NOT_SUPPORTED and never invents an animation request.
esp_err_t takeCodexAnimation(CodexAnimationRequest &request)
{
    (void)request;
    return ESP_ERR_NOT_SUPPORTED;
}

// request: Original caller identity; no simulated mailbox has admitted it.
// result: Original render outcome, never turned into an acknowledgement.
// Exception: Returns NOT_SUPPORTED without acknowledging a remote command.
esp_err_t completeCodexAnimation(const CodexAnimationRequest &request, esp_err_t result)
{
    (void)request;
    (void)result;
    return ESP_ERR_NOT_SUPPORTED;
}

// request: Destination remains unchanged; no simulated authenticated mailbox exists.
// Exception: Returns NOT_SUPPORTED and never fabricates a debug request.
esp_err_t takeCodexDebugScreen(CodexDebugScreenRequest &request)
{
    (void)request;
    return ESP_ERR_NOT_SUPPORTED;
}

// request: Original caller identity; no simulated mailbox has admitted it.
// result: Original render outcome, never turned into an acknowledgement.
// channel_count: Original renderer count, not returned to a physical device.
// Exception: Returns NOT_SUPPORTED without acknowledging a remote command.
esp_err_t completeCodexDebugScreen(const CodexDebugScreenRequest &request, esp_err_t result,
    uint8_t channel_count)
{
    (void)request;
    (void)result;
    (void)channel_count;
    return ESP_ERR_NOT_SUPPORTED;
}

// No parameters: host code has no device firmware-build metadata to report.
// Returns explicit Unavailable fields with static lifetime, never host identity.
const FirmwareBuildMetadata &firmware_build_metadata()
{
    static constexpr FirmwareBuildMetadata unavailable{};
    return unavailable;
}

} // namespace LUNASHELL
