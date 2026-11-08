#include "LUNASHELL_codex_saved.hpp"

#include <cstring>
#include <mutex>

#include "LUNASHELL_settings.hpp"
#include "esp_log.h"

namespace LUNASHELL::codex_saved {
namespace {

constexpr char kTag[] = "LUNASHELL_codex_saved";
constexpr uint32_t kVersion = 1;
constexpr int64_t kFirstEpoch = 1577836800;
constexpr int64_t kLastEpoch = 4102444799;
constexpr int64_t kInitialDelayUs = 2000000;
constexpr int64_t kWriteIntervalUs = 600000000;
constexpr uint8_t kVouchers = 1;

// Disk schema contains only fixed-width scalars and the retained pairing ID.
// A new binary layout requires a new version and key.
struct Record {
    int64_t weekly_observed_utc = 0;
    int64_t account_observed_utc = 0;
    int64_t weekly_reset_utc = 0;
    int64_t short_reset_utc = 0;
    uint64_t daily_tokens[CodexAccountSnapshot::kDailyBucketCount]{};
    uint64_t credit_milliunits = 0;
    uint32_t version = kVersion;
    uint32_t anchor_unix_day = 0;
    uint32_t reset_vouchers = 0;
    uint16_t weekly_remaining = 0;
    uint16_t short_remaining = 0;
    uint16_t short_minutes = 0;
    uint8_t daily_mask = 0;
    uint8_t credit_kind = 0;
    uint8_t flags = 0;
    char peer[33]{};
};
static_assert(sizeof(Record) == 160, "Keep the v1 checkpoint size stable");

std::mutex s_mutex;
Record s_record;
esp_err_t s_storage_error = ESP_OK;
// The CCP worker is the sole writer. These scheduling fields are worker-owned.
bool s_writable = false;
bool s_pending = false;
int64_t s_write_after_us = 0;

// epoch: UTC instant in the calendar range supported by the device countdown.
bool valid_epoch(int64_t epoch)
{
    return epoch >= kFirstEpoch && epoch <= kLastEpoch;
}

// day: Original date label from the dated account bucket response.
bool valid_day(uint32_t day)
{
    return day >= kFirstEpoch / 86400 && day <= kLastEpoch / 86400;
}

// peer: Complete lowercase hexadecimal PC ID with its terminator.
bool valid_peer(const char *peer)
{
    if (peer == nullptr) return false;
    for (size_t index = 0; index < 32; ++index) {
        const char value = peer[index];
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) return false;
    }
    return peer[32] == '\0';
}

// record: Exact-size checkpoint read through the owner and NVS integrity checks.
bool valid_record(const Record &record)
{
    const bool weekly = valid_epoch(record.weekly_observed_utc) && valid_epoch(record.weekly_reset_utc) &&
        record.weekly_remaining <= 10000;
    const bool no_weekly = record.weekly_observed_utc == 0 && record.weekly_reset_utc == 0 &&
        record.weekly_remaining == 0;
    const bool short_window = record.short_minutes == 0
        ? record.short_reset_utc == 0 && record.short_remaining == 0
        : valid_epoch(record.short_reset_utc) && record.short_remaining <= 10000;
    const bool account = valid_epoch(record.account_observed_utc) && short_window &&
        (record.daily_mask == 0 || valid_day(record.anchor_unix_day)) &&
        record.credit_kind <= static_cast<uint8_t>(CodexCreditKind::Unlimited) &&
        (record.flags & ~kVouchers) == 0;
    return record.version == kVersion && valid_peer(record.peer) &&
        (weekly || no_weekly) && (account || record.account_observed_utc == 0) && (weekly || account);
}

// sample: Successfully admitted account observation with a device-domain receipt time.
// age_ms: Age of the provider observation when the authenticated packet arrived.
int64_t observed_utc(const ComponentSnapshot &sample, uint32_t age_ms)
{
    if (!sample.data_valid || sample.data_stale || sample.health != DeviceHealth::Ready ||
        sample.last_error != ESP_OK || sample.last_success_at_us <= 0) return 0;
    ClockSnapshot clock{};
    const auto error = DeviceSnapshotService::instance().readClockObservation(clock);
    if (error != ESP_OK || !clock.data_valid || clock.data_stale || clock.health != DeviceHealth::Ready ||
        clock.last_error != ESP_OK || clock.last_success_at_us <= 0 || !valid_epoch(clock.utc_epoch_seconds)) return 0;
    const int64_t delta_us = sample.last_success_at_us - clock.last_success_at_us -
        static_cast<int64_t>(age_ms) * 1000;
    // The canonical clock publishes whole UTC seconds. Round negative offsets down.
    const int64_t delta_seconds = delta_us / 1000000 - (delta_us % 1000000 < 0 ? 1 : 0);
    const int64_t epoch = clock.utc_epoch_seconds + delta_seconds;
    return valid_epoch(epoch) ? epoch : 0;
}

// peer: Authenticated identity of the packet. The caller holds the saved-data mutex.
void select_peer(const char *peer)
{
    if (std::strcmp(s_record.peer, peer) != 0) {
        s_record = {};
        std::memcpy(s_record.peer, peer, sizeof(s_record.peer));
    }
}

// received_us: Monotonic receipt time of the successfully admitted sample.
void schedule_write(int64_t received_us)
{
    if (s_write_after_us == 0) s_write_after_us = received_us + kInitialDelayUs;
    s_pending = true;
}

} // namespace

// No parameters: load once at CCP startup, before concurrent worker updates.
void load()
{
    Record loaded{};
    auto error = UserSettingsService::instance().getBlobExact(kCheckpointKey, &loaded, sizeof(loaded));
    const bool absent = error == ESP_ERR_NVS_NOT_FOUND;
    if (error == ESP_OK && !valid_record(loaded)) error = ESP_ERR_INVALID_RESPONSE;
    std::lock_guard<std::mutex> guard(s_mutex);
    s_writable = error == ESP_OK || absent;
    s_storage_error = absent ? ESP_OK : error;
    if (error == ESP_OK) s_record = loaded;
    if (s_storage_error != ESP_OK) {
        ESP_LOGE(kTag, "Saved Codex checkpoint read failed (%s)", esp_err_to_name(s_storage_error));
    }
}

// peer: Authenticated PC identity of this observation.
// sample: Published weekly limit with its original receipt and source age.
void remember_weekly(const char *peer, const CodexWeeklyLimitSnapshot &sample)
{
    if (!valid_peer(peer) || !valid_epoch(sample.reset_unix_seconds) ||
        sample.remaining_basis_points > 10000) return;
    const int64_t epoch = observed_utc(sample, sample.source_age_at_receive_ms);
    if (epoch == 0) return;
    std::lock_guard<std::mutex> guard(s_mutex);
    select_peer(peer);
    s_record.weekly_observed_utc = epoch;
    s_record.weekly_reset_utc = sample.reset_unix_seconds;
    s_record.weekly_remaining = sample.remaining_basis_points;
    schedule_write(sample.last_success_at_us);
}

// peer: Authenticated PC identity of this observation.
// sample: Published account details with their original receipt and source age.
void remember_account(const char *peer, const CodexAccountSnapshot &sample)
{
    if (!valid_peer(peer) || (sample.short_window_minutes == 0 && sample.daily_valid_mask == 0 &&
        sample.credits_kind == CodexCreditKind::Unavailable && !sample.reset_vouchers_valid)) return;
    if ((sample.short_window_minutes != 0 &&
        (!valid_epoch(sample.short_reset_unix_seconds) || sample.short_remaining_basis_points > 10000)) ||
        (sample.daily_valid_mask != 0 && !valid_day(sample.anchor_unix_day))) return;
    const int64_t epoch = observed_utc(sample, sample.source_age_at_receive_ms);
    if (epoch == 0) return;
    std::lock_guard<std::mutex> guard(s_mutex);
    select_peer(peer);
    s_record.account_observed_utc = epoch;
    s_record.short_reset_utc = sample.short_reset_unix_seconds;
    s_record.short_remaining = sample.short_remaining_basis_points;
    s_record.short_minutes = sample.short_window_minutes;
    s_record.anchor_unix_day = sample.anchor_unix_day;
    s_record.daily_mask = sample.daily_valid_mask;
    std::memcpy(s_record.daily_tokens, sample.daily_tokens, sizeof(s_record.daily_tokens));
    s_record.credit_milliunits = sample.credit_milliunits;
    s_record.credit_kind = static_cast<uint8_t>(sample.credits_kind);
    s_record.reset_vouchers = sample.reset_vouchers;
    s_record.flags = sample.reset_vouchers_valid ? kVouchers : 0;
    schedule_write(sample.last_success_at_us);
}

// now_us: Monotonic worker time for coalescing and flash write pacing.
void checkpoint(int64_t now_us)
{
    if (!s_writable || !s_pending || now_us < s_write_after_us) return;
    Record pending{};
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        pending = s_record;
    }
    // Settings executor work never holds either the reader lock or the CCP lock.
    const auto error = valid_record(pending)
        ? UserSettingsService::instance().setBlob(kCheckpointKey, &pending, sizeof(pending))
        : ESP_ERR_INVALID_RESPONSE;
    s_write_after_us = now_us + kWriteIntervalUs;
    s_pending = error != ESP_OK;
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        s_storage_error = error;
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Saved Codex checkpoint write failed (%s)", esp_err_to_name(error));
    }
}

// No parameters: pairing owner confirms durable absence before releasing old historical data.
void reset_after_erase()
{
    std::lock_guard<std::mutex> guard(s_mutex);
    s_record = {};
    s_storage_error = ESP_OK;
    s_writable = true;
    s_pending = false;
    s_write_after_us = 0;
}

// peer: Current retained pairing identity, never a replacement candidate.
// saved: Receives only dated, historical values belonging to that pairing.
void read(const char *peer, CodexSavedSnapshot &saved)
{
    std::lock_guard<std::mutex> guard(s_mutex);
    saved = {};
    saved.storage_error = s_storage_error;
    if (peer[0] == '\0' || std::strcmp(s_record.peer, peer) != 0) return;
    saved.weekly_observed_utc = s_record.weekly_observed_utc;
    saved.account_observed_utc = s_record.account_observed_utc;
    saved.weekly.reset_unix_seconds = s_record.weekly_reset_utc;
    saved.weekly.remaining_basis_points = s_record.weekly_remaining;
    saved.weekly.data_stale = saved.weekly_observed_utc > 0;
    auto &account = saved.account;
    account.short_reset_unix_seconds = s_record.short_reset_utc;
    account.short_remaining_basis_points = s_record.short_remaining;
    account.short_window_minutes = s_record.short_minutes;
    account.anchor_unix_day = s_record.anchor_unix_day;
    account.daily_valid_mask = s_record.daily_mask;
    std::memcpy(account.daily_tokens, s_record.daily_tokens, sizeof(account.daily_tokens));
    account.credit_milliunits = s_record.credit_milliunits;
    account.credits_kind = static_cast<CodexCreditKind>(s_record.credit_kind);
    account.reset_vouchers = s_record.reset_vouchers;
    account.reset_vouchers_valid = (s_record.flags & kVouchers) != 0;
    account.data_stale = saved.account_observed_utc > 0;
}

} // namespace LUNASHELL::codex_saved
