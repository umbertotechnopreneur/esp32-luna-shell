/* Copyright (c) 2026 Umberto Giacobbi */
#include "LUNASHELL_codex_state_saved.hpp"

#include "LUNASHELL_settings.hpp"

#include <array>
#include <cstring>
#include <mutex>

#include "esp_log.h"
#include "nvs.h"
#include "psa/crypto.h"

namespace LUNASHELL::codex_state_saved {
namespace {

using CODY::Domain::StateSummary;

constexpr char kTag[] = "CodyStateSaved";
constexpr char kCheckpointKey[] = "ccp_domain_v1";
constexpr char kMacDomain[] = "cody.state.checkpoint/1";
constexpr uint64_t kInitialDelayUs = 2000000;
constexpr uint64_t kWriteIntervalUs = 600000000;
// Wire-independent, exact-size checkpoint:
// magic(4) | PC ID(16) | source/epoch/revision(24) | four counts(16) | HMAC(32).
// Multi-byte integers are big endian. No clock, credentials or entity text is saved.
constexpr size_t kPayloadBytes = 60;
constexpr size_t kTagBytes = 32;
using Record = std::array<uint8_t, kPayloadBytes + kTagBytes>;

std::mutex s_mutex;
StateSummary s_durable{};
StateSummary s_pending{};
esp_err_t s_error = ESP_OK;
uint64_t s_next_write_us = 0;
uint64_t s_last_seen_us = 0;
bool s_writable = false;

// bytes: Destination of the exact scalar width.
// width: Number of big-endian bytes, up to eight.
// value: Value fitting in that width.
void encode(uint8_t *bytes, size_t width, uint64_t value)
{
    for (size_t i = width; i > 0; --i) {
        bytes[i - 1] = static_cast<uint8_t>(value);
        value >>= 8U;
    }
}

// bytes: Source of an already length-bounded scalar.
// width: Number of big-endian bytes, up to eight.
uint64_t decode(const uint8_t *bytes, size_t width)
{
    uint64_t value = 0;
    for (size_t i = 0; i < width; ++i) value = (value << 8U) | bytes[i];
    return value;
}

// left: Current last verified status.
// right: New observation to consider for coalescing.
bool same(const StateSummary &left, const StateSummary &right)
{
    return left.source == right.source && left.epoch == right.epoch && left.revision == right.revision &&
        left.chats == right.chats && left.turns == right.turns && left.active_calls == right.active_calls &&
        left.terminal_calls == right.terminal_calls;
}

// summary: Bounded device-generation diagnostic values.
bool valid(const StateSummary &summary)
{
    constexpr CODY::Domain::CapacityLimits limits{};
    return summary.source.valid() && summary.epoch.valid() && summary.revision != 0 &&
        summary.chats <= limits.chats && summary.turns <= limits.turns &&
        summary.active_calls <= limits.active_calls && summary.terminal_calls <= limits.terminal_calls;
}

// record: Verified checkpoint with the exact wire-independent layout.
// peer: Current persisted PC ID.
// key: Current persisted link key.
// summary: Destination assigned only after all integrity and range checks pass.
esp_err_t decode_record(Record &record, const uint8_t *peer, const uint8_t *key, StateSummary &summary)
{
    if (std::memcmp(record.data(), "CDR1", 4) != 0) return ESP_ERR_INVALID_VERSION;
    if (std::memcmp(record.data() + 4, peer, 16) != 0) return ESP_ERR_INVALID_STATE;
    const auto authenticated = authenticate_checkpoint(record.data(), kPayloadBytes, kMacDomain, key, true);
    if (authenticated != ESP_OK) return authenticated;
    StateSummary candidate{};
    candidate.source.value = decode(record.data() + 20, 8);
    candidate.epoch.value = decode(record.data() + 28, 8);
    candidate.revision = decode(record.data() + 36, 8);
    candidate.chats = static_cast<uint32_t>(decode(record.data() + 44, 4));
    candidate.turns = static_cast<uint32_t>(decode(record.data() + 48, 4));
    candidate.active_calls = static_cast<uint32_t>(decode(record.data() + 52, 4));
    candidate.terminal_calls = static_cast<uint32_t>(decode(record.data() + 56, 4));
    if (!valid(candidate)) return ESP_ERR_INVALID_RESPONSE;
    summary = candidate;
    return ESP_OK;
}

// summary: One complete authenticated generation's bounded status.
// peer: Current persisted PC ID.
// key: Corresponding active key, never written to flash.
// record: Exact, versioned checkpoint with a domain-separated authentication tag.
esp_err_t encode_record(const StateSummary &summary, const uint8_t *peer, const uint8_t *key, Record &record)
{
    if (!valid(summary)) return ESP_ERR_INVALID_RESPONSE;
    std::memcpy(record.data(), "CDR1", 4);
    std::memcpy(record.data() + 4, peer, 16);
    encode(record.data() + 20, 8, summary.source.value);
    encode(record.data() + 28, 8, summary.epoch.value);
    encode(record.data() + 36, 8, summary.revision);
    encode(record.data() + 44, 4, summary.chats);
    encode(record.data() + 48, 4, summary.turns);
    encode(record.data() + 52, 4, summary.active_calls);
    encode(record.data() + 56, 4, summary.terminal_calls);
    return authenticate_checkpoint(record.data(), kPayloadBytes, kMacDomain, key, false);
}

} // namespace

// record: Exact payload followed by the 32-byte authentication tag.
// payload_bytes: Number of authenticated record bytes, excluding the tag.
// domain: NUL-terminated purpose label included in the MAC.
// key: Retained active key. The imported PSA object is destroyed before returning.
// verify: True to verify in constant time, false to write the MAC.
esp_err_t authenticate_checkpoint(uint8_t *record, size_t payload_bytes, const char *domain,
                                  const uint8_t *key, bool verify)
{
    if (!record || !domain || !key || payload_bytes > 256) return ESP_ERR_INVALID_ARG;
    const size_t domain_bytes = std::strlen(domain) + 1;
    if (domain_bytes > 40) return ESP_ERR_INVALID_ARG;
    std::array<uint8_t, 296> input{};
    std::memcpy(input.data(), domain, domain_bytes);
    std::memcpy(input.data() + domain_bytes, record, payload_bytes);
    auto *const tag = record + payload_bytes;

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, verify ? PSA_KEY_USAGE_VERIFY_MESSAGE : PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    mbedtls_svc_key_id_t imported = MBEDTLS_SVC_KEY_ID_INIT;
    auto status = psa_import_key(&attributes, key, 32, &imported);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) {
        ESP_LOGE(kTag, "Checkpoint MAC key import failed: PSA status %ld", static_cast<long>(status));
        return ESP_FAIL;
    }
    size_t written = 0;
    if (verify) {
        status = psa_mac_verify(imported, PSA_ALG_HMAC(PSA_ALG_SHA_256),
            input.data(), domain_bytes + payload_bytes, tag, kTagBytes);
    } else {
        status = psa_mac_compute(imported, PSA_ALG_HMAC(PSA_ALG_SHA_256),
            input.data(), domain_bytes + payload_bytes, tag, kTagBytes, &written);
    }
    const auto destroy = psa_destroy_key(imported);
    if (destroy != PSA_SUCCESS) {
        ESP_LOGE(kTag, "Checkpoint MAC key destroy failed: PSA status %ld", static_cast<long>(destroy));
    }
    if (status == PSA_ERROR_INVALID_SIGNATURE && verify) return ESP_ERR_INVALID_CRC;
    if (status != PSA_SUCCESS) {
        ESP_LOGE(kTag, "Checkpoint MAC operation failed: PSA status %ld", static_cast<long>(status));
        return ESP_FAIL;
    }
    if (destroy != PSA_SUCCESS) return ESP_FAIL;
    return verify || written == kTagBytes ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

// peer: Current retained PC ID, or null when no active association exists.
// key: Corresponding active retained key, or null when unpaired.
void load(const uint8_t *peer, const uint8_t *key)
{
    Record record{};
    StateSummary summary{};
    esp_err_t error = ESP_ERR_NVS_NOT_FOUND;
    if (peer && key) {
        error = UserSettingsService::instance().getBlobExact(kCheckpointKey, record.data(), record.size());
        if (error == ESP_OK) error = decode_record(record, peer, key, summary);
    }
    const bool absent = error == ESP_ERR_NVS_NOT_FOUND;
    std::lock_guard<std::mutex> guard(s_mutex);
    s_durable = summary;
    s_pending = {};
    s_next_write_us = 0;
    s_last_seen_us = 0;
    s_writable = peer && key && (absent || error == ESP_OK);
    s_error = absent ? ESP_OK : error;
    if (s_error != ESP_OK) {
        ESP_LOGE(kTag, "State checkpoint could not be restored (%s)", esp_err_to_name(s_error));
    }
}

// summary: Last complete authenticated generation's diagnostic values.
// now_us: Device monotonic time, never persisted.
void remember(const StateSummary &summary, uint64_t now_us)
{
    std::lock_guard<std::mutex> guard(s_mutex);
    if (!s_writable) return;
    if (!valid(summary)) {
        s_error = ESP_ERR_INVALID_RESPONSE;
        ESP_LOGE(kTag, "State checkpoint rejected an invalid generation summary");
        return;
    }
    if (now_us < s_last_seen_us) s_next_write_us = now_us + kInitialDelayUs;
    s_last_seen_us = now_us;
    s_pending = same(summary, s_durable) ? StateSummary{} : summary;
    if (s_next_write_us == 0) s_next_write_us = now_us + kInitialDelayUs;
}

// peer: Current persisted PC ID supplied by the sole CCP worker.
// key: Corresponding key, borrowed through the synchronous write/readback.
// now_us: Monotonic time used to coalesce updates outside the CCP lock.
void checkpoint(const uint8_t *peer, const uint8_t *key, uint64_t now_us)
{
    StateSummary pending{};
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (!s_writable || s_pending.revision == 0) return;
        if (now_us < s_last_seen_us) s_next_write_us = now_us + kInitialDelayUs;
        s_last_seen_us = now_us;
        if (now_us < s_next_write_us) return;
        pending = s_pending;
    }

    Record record{};
    auto error = encode_record(pending, peer, key, record);
    auto &settings = UserSettingsService::instance();
    if (error == ESP_OK) error = settings.setBlob(kCheckpointKey, record.data(), record.size());
    bool readback_failed = false;
    if (error == ESP_OK) {
        Record readback{};
        error = settings.getBlobExact(kCheckpointKey, readback.data(), readback.size());
        if (error == ESP_OK && readback != record) error = ESP_ERR_INVALID_RESPONSE;
        readback_failed = error != ESP_OK;
    }

    std::lock_guard<std::mutex> guard(s_mutex);
    s_next_write_us = now_us + kWriteIntervalUs;
    s_error = error;
    if (error == ESP_OK) {
        s_durable = pending;
        s_pending = {};
    } else {
        if (readback_failed) s_writable = false;
        ESP_LOGE(kTag, "State checkpoint write/readback failed (%s)", esp_err_to_name(error));
    }
}

// summary: Historical diagnostic checkpoint if one was verified.
// storage_error: Settings failure copied atomically with the checkpoint.
bool try_read(StateSummary &summary, esp_err_t &storage_error)
{
    std::unique_lock<std::mutex> guard(s_mutex, std::try_to_lock);
    if (!guard.owns_lock()) return false;
    summary = s_durable;
    storage_error = s_error;
    return true;
}

// No parameters: explicit association retirement with verified NVS absence.
esp_err_t erase()
{
    auto &settings = UserSettingsService::instance();
    auto error = settings.eraseKey(kCheckpointKey);
    if (error == ESP_OK || error == ESP_ERR_NVS_NOT_FOUND) {
        Record remaining{};
        const auto readback = settings.getBlobExact(kCheckpointKey, remaining.data(), remaining.size());
        error = readback == ESP_ERR_NVS_NOT_FOUND ? ESP_OK :
            readback == ESP_OK ? ESP_ERR_INVALID_RESPONSE : readback;
    }
    std::lock_guard<std::mutex> guard(s_mutex);
    s_error = error;
    if (error == ESP_OK) {
        s_durable = {};
        s_pending = {};
        s_writable = true;
        s_next_write_us = 0;
        s_last_seen_us = 0;
    } else {
        s_writable = false;
        ESP_LOGE(kTag, "State checkpoint removal failed (%s)", esp_err_to_name(error));
    }
    return error;
}

} // namespace LUNASHELL::codex_state_saved
