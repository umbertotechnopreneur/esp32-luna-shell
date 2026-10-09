#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"

#include "sdkconfig.h"

#if CONFIG_LUNASHELL_BUILD_PROFILE_CODY_COMPANION && \
    CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNA_SHELL_CODY_ENABLE_CODEX

#include "LUNASHELL_settings.hpp"
#include "LUNASHELL_task_affinity.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "esp_log.h"
#include "esp_attr.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "psa/crypto.h"
#include "unistd.h"

namespace {

constexpr char kTag[] = "LUNASHELL_codex";
constexpr char kProtocol[] = "CCP1";
constexpr char kPairingSetting[] = "ccp_pairing";
constexpr char kPendingPairingSetting[] = "ccp_pending";
constexpr char kCodeAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr uint16_t kDevicePort = 42117;
constexpr uint16_t kPluginPort = 42118;
constexpr size_t kMaximumDatagramBytes = 512;
constexpr size_t kMaximumEventDatagramBytes = 1280;
constexpr uint32_t kPbkdf2Iterations = 50000;
constexpr int64_t kDerivationSliceUs = 10000;
constexpr int64_t kPairingWindowUs = 5LL * 60LL * 1000000LL;
constexpr int64_t kFindIntervalUs = 2LL * 1000000LL;
constexpr int64_t kReadyExpiryUs = 8LL * 1000000LL;
constexpr int64_t kActivationReplayUs = 10LL * 1000000LL;
constexpr uint32_t kWorkerPollMs = 100;
constexpr size_t kWorkerStackSize = 8192;
constexpr UBaseType_t kWorkerPriority = 1;
constexpr uint8_t kPersistedVersion = 1;
static_assert(sizeof(kCodeAlphabet) == 33, "CCP display alphabet must contain exactly 32 symbols");
using State = LUNASHELL::CodexLinkState;

enum class Command : uint8_t {
    None,
    StartPairing,
    StartDiscovery,
    Cancel,
    ForgetPairing,
};

struct PersistedLink {
    uint8_t version = kPersistedVersion;
    uint8_t reserved[3]{};
    uint8_t pc_id[16]{};
    uint8_t link_key[32]{};
    uint32_t crc32 = 0;
};

static_assert(sizeof(PersistedLink) == 56, "CCP persisted link must remain byte-stable");

std::mutex s_mutex;
StaticTask_t s_worker_control{};
// CCP does socket/crypto work; all persisted writes run on the internal settings executor.
EXT_RAM_BSS_ATTR StackType_t s_worker_stack[kWorkerStackSize]{};
TaskHandle_t s_worker = nullptr;
Command s_pending = Command::None;
int s_socket = -1;
LUNASHELL::CodexLinkSnapshot s_link_snapshot{};
PersistedLink s_persisted_link{};
bool s_link_present = false;
PersistedLink s_candidate_link{};
bool s_candidate_present = false;
esp_err_t s_storage_error = ESP_OK;
char s_pairing_code[7]{};
uint8_t s_pairing_salt[16]{};
uint8_t s_pairing_verifier[32]{};
uint32_t s_pairing_session_id = 0;
char s_pairing_pc_id[33]{};
char s_pairing_nonce[33]{};
char s_activation_session_id[9]{};
char s_activation_pc_id[33]{};
char s_activation_nonce[33]{};
int64_t s_activation_replay_until_us = 0;
uint8_t s_find_nonce[16]{};
int64_t s_next_find_at_us = 0;
int64_t s_last_ready_at_us = 0;
bool s_activity_received_for_nonce = false;
bool s_event_received_for_nonce = false;
char s_ready_nonce[33]{};
LUNASHELL::CodexEventSnapshot s_event_snapshot{};
LUNASHELL::CodexEventPayload s_event_candidate{};
char s_dismissed_event_id[33]{};
bool s_event_publication_pending = false;
bool s_reply_pending = false;
int64_t s_next_reply_at_us = 0;
char s_reply_nonce[33]{};
// Only the CCP worker borrows packet/crypto scratch; UI calls submit identities only.
EXT_RAM_BSS_ATTR char s_receive_buffer[kMaximumEventDatagramBytes + 2]{};
EXT_RAM_BSS_ATTR char s_event_parse_buffer[kMaximumEventDatagramBytes + 1]{};
EXT_RAM_BSS_ATTR char s_event_proof_input[kMaximumEventDatagramBytes + 1]{};

// bytes: Secret or transient storage to erase with volatile writes.
// size: Exact byte count owned by the caller.
void wipe(void *bytes, size_t size)
{
    volatile uint8_t *cursor = static_cast<volatile uint8_t *>(bytes);
    while (size-- != 0) {
        *cursor++ = 0;
    }
}

// record: Fixed persisted bytes excluding the CRC field itself.
uint32_t persistedLinkCrc32(const PersistedLink &record)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(&record);
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < offsetof(PersistedLink, crc32); ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(
                -static_cast<int32_t>(crc & 1U)
            );
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

// bytes: Binary value to inspect without exposing it in logs.
// size: Exact byte count.
bool allZero(const uint8_t *bytes, size_t size)
{
    uint8_t combined = 0;
    for (size_t index = 0; index < size; ++index) {
        combined |= bytes[index];
    }
    return combined == 0;
}

// left: First fixed-length proof.
// right: Second fixed-length proof.
// size: Equal byte count for both values.
bool constantTimeEqual(const uint8_t *left, const uint8_t *right, size_t size)
{
    uint8_t difference = 0;
    for (size_t index = 0; index < size; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0;
}

// bytes: Binary source value.
// size: Exact source length.
// output: Destination with room for two lowercase characters per byte and a terminator.
// output_size: Exact writable destination capacity.
// Exception: Returns ESP_ERR_INVALID_SIZE when the destination cannot hold the complete value.
esp_err_t encodeHex(
    const uint8_t *bytes,
    size_t size,
    char *output,
    size_t output_size
)
{
    static constexpr char digits[] = "0123456789abcdef";
    if (bytes == nullptr || output == nullptr || output_size < size * 2U + 1U) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t index = 0; index < size; ++index) {
        output[index * 2U] = digits[bytes[index] >> 4U];
        output[index * 2U + 1U] = digits[bytes[index] & 0x0FU];
    }
    output[size * 2U] = '\0';
    return ESP_OK;
}

// value: Lowercase hexadecimal input of one exact expected length.
// output: Binary destination.
// output_size: Exact expected binary size.
// Exception: Returns ESP_ERR_INVALID_ARG for uppercase, non-hex or wrong-length input.
esp_err_t decodeHex(const char *value, uint8_t *output, size_t output_size)
{
    if (value == nullptr || output == nullptr || std::strlen(value) != output_size * 2U) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t index = 0; index < output_size; ++index) {
        uint8_t byte = 0;
        for (size_t digit_index = 0; digit_index < 2U; ++digit_index) {
            const char digit = value[index * 2U + digit_index];
            uint8_t nibble = 0;
            if (digit >= '0' && digit <= '9') {
                nibble = static_cast<uint8_t>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                nibble = static_cast<uint8_t>(digit - 'a' + 10);
            } else {
                return ESP_ERR_INVALID_ARG;
            }
            byte = static_cast<uint8_t>((byte << 4U) | nibble);
        }
        output[index] = byte;
    }
    return ESP_OK;
}

// operation: Non-secret crypto operation name.
// status: Exact PSA failure code retained in the log.
esp_err_t logCryptoFailure(const char *operation, psa_status_t status)
{
    ESP_LOGE(kTag, "%s failed: PSA status %ld", operation, static_cast<long>(status));
    return ESP_FAIL;
}

// key: HMAC key retained only by the current operation owner.
// key_size: Exact key length.
// message: Canonical printable-ASCII proof input.
// output: Thirty-two byte SHA-256 HMAC destination.
// Exception: Returns the exact local setup failure and logs the PSA cause.
esp_err_t hmacSha256(
    const uint8_t *key,
    size_t key_size,
    const char *message,
    uint8_t output[32]
)
{
    if (key == nullptr || key_size == 0 || message == nullptr || output == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, key_size * 8U);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    mbedtls_svc_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    psa_status_t status = psa_import_key(&attributes, key, key_size, &key_id);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) {
        return logCryptoFailure("CCP HMAC key import", status);
    }
    size_t output_length = 0;
    status = psa_mac_compute(
        key_id,
        PSA_ALG_HMAC(PSA_ALG_SHA_256),
        reinterpret_cast<const uint8_t *>(message),
        std::strlen(message),
        output,
        32,
        &output_length
    );
    const psa_status_t destroy_status = psa_destroy_key(key_id);
    if (status != PSA_SUCCESS) {
        return logCryptoFailure("CCP HMAC", status);
    }
    if (destroy_status != PSA_SUCCESS) {
        return logCryptoFailure("CCP HMAC key destroy", destroy_status);
    }
    return output_length == 32 ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

// code: Six-character display-only pairing code.
// salt: Sixteen-byte random salt for this attempt.
// verifier: Thirty-two byte derived verifier destination.
// Exception: Returns the PSA computation or key-cleanup failure without a verifier.
esp_err_t deriveVerifier(
    const char code[7],
    const uint8_t salt[16],
    uint8_t verifier[32]
)
{
    // PBKDF2's single SHA-256 output block: U1 = HMAC(code, salt || INT32_BE(1));
    // verifier = U1 xor ... xor U50000. PSA's monolithic KDF has no scheduling
    // hook, so sequence its official HMAC primitive with bounded idle admission.
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    mbedtls_svc_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    psa_status_t status = psa_import_key(&attributes,
        reinterpret_cast<const uint8_t *>(code), 6, &key_id);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) {
        return logCryptoFailure("CCP PBKDF2 key import", status);
    }
    const int64_t derivation_started_us = esp_timer_get_time();
    ESP_LOGI(kTag, "CCP verifier HMAC key admitted; cooperative slices=%lld us",
        static_cast<long long>(kDerivationSliceUs));

    uint8_t salt_block[20]{};
    std::memcpy(salt_block, salt, 16);
    salt_block[19] = 1;
    uint8_t previous[32]{};
    uint8_t current[32]{};
    uint8_t accumulated[32]{};
    size_t output_size = 0;
    status = psa_mac_compute(key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256),
        salt_block, sizeof(salt_block), previous, sizeof(previous), &output_size);
    esp_err_t error = ESP_OK;
    if (status == PSA_SUCCESS && output_size != sizeof(previous)) {
        error = ESP_ERR_INVALID_SIZE;
        ESP_LOGE(kTag, "CCP PBKDF2 initial HMAC length rejected: %u (%s)",
            static_cast<unsigned>(output_size), esp_err_to_name(error));
    }
    if (status == PSA_SUCCESS && error == ESP_OK) {
        std::memcpy(accumulated, previous, sizeof(accumulated));
        int64_t slice_started_us = esp_timer_get_time();
        for (uint32_t iteration = 1; iteration < kPbkdf2Iterations; ++iteration) {
            // Distinct input/output storage avoids depending on PSA alias support.
            status = psa_mac_compute(key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256),
                previous, sizeof(previous), current, sizeof(current), &output_size);
            if (status != PSA_SUCCESS) {
                break;
            }
            if (output_size != sizeof(current)) {
                error = ESP_ERR_INVALID_SIZE;
                ESP_LOGE(kTag, "CCP PBKDF2 HMAC length rejected: %u (%s)",
                    static_cast<unsigned>(output_size), esp_err_to_name(error));
                break;
            }
            for (size_t index = 0; index < sizeof(accumulated); ++index) {
                accumulated[index] ^= current[index];
            }
            std::memcpy(previous, current, sizeof(previous));
            if ((iteration + 1U) % 5000U == 0) {
                ESP_LOGI(kTag, "CCP verifier progress: iterations=%lu/%lu elapsed_us=%lld",
                    static_cast<unsigned long>(iteration + 1U),
                    static_cast<unsigned long>(kPbkdf2Iterations),
                    static_cast<long long>(esp_timer_get_time() - derivation_started_us));
            }
            if (esp_timer_get_time() - slice_started_us >= kDerivationSliceUs) {
                // A real blocked tick admits IDLE0; taskYIELD alone would not.
                // No service/snapshot/PSA operation lock survives this boundary.
                vTaskDelay(1);
                slice_started_us = esp_timer_get_time();
            }
        }
    }
    if (status != PSA_SUCCESS) {
        error = logCryptoFailure("CCP PBKDF2 HMAC", status);
    }
    const psa_status_t destroy_status = psa_destroy_key(key_id);
    if (destroy_status != PSA_SUCCESS) {
        const esp_err_t cleanup_error = logCryptoFailure("CCP PBKDF2 key destroy", destroy_status);
        if (error == ESP_OK) {
            error = cleanup_error;
        }
    }
    if (error == ESP_OK) {
        std::memcpy(verifier, accumulated, sizeof(accumulated));
    }
    wipe(salt_block, sizeof(salt_block));
    wipe(previous, sizeof(previous));
    wipe(current, sizeof(current));
    wipe(accumulated, sizeof(accumulated));
    return error;
}

// state: Confirmed service state or admitted pending command.
// error: Original operation error.
void publishLocked(State state, esp_err_t error)
{
    const int64_t now_us = esp_timer_get_time();
    s_link_snapshot.state = state;
    s_link_snapshot.last_error = error;
    s_link_snapshot.changed_at_us = now_us;
    s_link_snapshot.last_attempt_at_us = now_us;
    s_link_snapshot.command_pending = s_pending != Command::None;
    s_link_snapshot.pairing_window_active = state == State::StartingPairing || state == State::Pairing;
    s_link_snapshot.retained_link_present = s_link_present;
    s_link_snapshot.pending_pairing_present = s_candidate_present;
    s_link_snapshot.pairing_repair_required = s_storage_error != ESP_OK;
    s_link_snapshot.data_valid = true;
    s_link_snapshot.data_stale = false;
    s_link_snapshot.health = error == ESP_OK
        ? LUNASHELL::DeviceHealth::Ready
        : LUNASHELL::DeviceHealth::Degraded;
    if (state != State::Connected && state != State::Discovering) {
        s_link_snapshot.activity.data_valid = false;
        s_link_snapshot.activity.data_stale = s_link_snapshot.activity.last_success_at_us > 0;
        s_link_snapshot.activity.last_error = error != ESP_OK ? error : ESP_ERR_INVALID_STATE;
        s_link_snapshot.activity.health = LUNASHELL::DeviceHealth::Degraded;
    }
    if (error == ESP_OK) {
        s_link_snapshot.last_success_at_us = now_us;
    }
    LUNASHELL::DeviceSnapshotService::instance().reportCodexLink(s_link_snapshot);
}

// No parameters: erases the current display code and ephemeral pairing secrets.
void clearPairingSecretsLocked()
{
    wipe(s_pairing_code, sizeof(s_pairing_code));
    wipe(s_pairing_salt, sizeof(s_pairing_salt));
    wipe(s_pairing_verifier, sizeof(s_pairing_verifier));
    wipe(s_pairing_pc_id, sizeof(s_pairing_pc_id));
    wipe(s_pairing_nonce, sizeof(s_pairing_nonce));
    s_pairing_session_id = 0;
    s_link_snapshot.deadline_at_us = 0;
    s_link_snapshot.pairing_window_active = false;
}

// No parameters: clears the short non-secret context used only to replay ACTIVE.
void clearActivationReplayLocked()
{
    wipe(s_activation_session_id, sizeof(s_activation_session_id));
    wipe(s_activation_pc_id, sizeof(s_activation_pc_id));
    wipe(s_activation_nonce, sizeof(s_activation_nonce));
    s_activation_replay_until_us = 0;
}

// observation: Canonical connectivity projection, never a live driver read.
bool wifiOnline(const LUNASHELL::ConnectivityObservation &observation)
{
    const auto &wifi = observation.radios.wifi;
    // Paired Cody retains its radio owners during software standby. Canonical
    // Wi-Fi readiness, not the display state, admits authenticated traffic.
    return (observation.operating_state == LUNASHELL::DeviceOperatingState::Active ||
        observation.operating_state == LUNASHELL::DeviceOperatingState::Standby) &&
        wifi.data_valid && !wifi.data_stale &&
        wifi.health == LUNASHELL::DeviceHealth::Ready &&
        wifi.last_error == ESP_OK && wifi.connected && wifi.ip_acquired &&
        wifi.ip_address[0] != '\0' && wifi.mac_address[0] != '\0';
}

// mac_address: Canonical colon-separated station MAC.
// device_id: Twelve-character lowercase protocol identity destination.
// Exception: Returns ESP_ERR_INVALID_ARG for a malformed canonical value.
esp_err_t formatDeviceId(const char *mac_address, char device_id[13])
{
    if (mac_address == nullptr || std::strlen(mac_address) != 17) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t output_index = 0;
    for (size_t index = 0; index < 17; ++index) {
        if (index == 2 || index == 5 || index == 8 || index == 11 || index == 14) {
            if (mac_address[index] != ':') {
                return ESP_ERR_INVALID_ARG;
            }
            continue;
        }
        char value = mac_address[index];
        if (value >= 'A' && value <= 'F') {
            value = static_cast<char>(value - 'A' + 'a');
        }
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) {
            return ESP_ERR_INVALID_ARG;
        }
        device_id[output_index++] = value;
    }
    device_id[output_index] = '\0';
    return output_index == 12 ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

// key: Active or candidate record owned by the CCP service.
// record: Receives validated bytes only; never exposed through a snapshot or log.
// present: Receives false only for a genuinely absent key.
// Exception: Returns corrupt, partial, inaccessible or unsupported storage unchanged.
esp_err_t readLinkRecord(const char *key, PersistedLink &record, bool &present)
{
    auto &settings = LUNASHELL::UserSettingsService::instance();
    std::vector<uint8_t> data;
    esp_err_t error = settings.getBlob(key, &data);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "CCP record '%s' absent; no identity is invented", key);
        present = false;
        wipe(&record, sizeof(record));
        return ESP_OK;
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "CCP record '%s' read failed (%s)", key, esp_err_to_name(error));
        wipe(data.data(), data.size());
        return error;
    }
    if (data.size() != sizeof(PersistedLink)) {
        ESP_LOGE(kTag, "CCP pairing record size %u is invalid; explicit repair required",
                 static_cast<unsigned>(data.size()));
        wipe(data.data(), data.size());
        return ESP_ERR_INVALID_SIZE;
    }
    std::memcpy(&record, data.data(), sizeof(record));
    wipe(data.data(), data.size());
    if (record.version != kPersistedVersion) {
        ESP_LOGE(kTag, "CCP pairing record version %u is unsupported; explicit repair required",
                 static_cast<unsigned>(record.version));
        wipe(&record, sizeof(record));
        return ESP_ERR_NOT_SUPPORTED;
    }
    const uint32_t expected_crc = persistedLinkCrc32(record);
    if (record.crc32 != expected_crc) {
        ESP_LOGE(kTag, "CCP pairing record CRC mismatch; explicit repair required");
        wipe(&record, sizeof(record));
        return ESP_ERR_INVALID_CRC;
    }
    if (!allZero(record.reserved, sizeof(record.reserved)) ||
        allZero(record.pc_id, sizeof(record.pc_id)) ||
        allZero(record.link_key, sizeof(record.link_key))) {
        ESP_LOGE(kTag, "CCP pairing record identity is invalid; explicit repair required");
        wipe(&record, sizeof(record));
        return ESP_ERR_INVALID_RESPONSE;
    }
    present = true;
    return ESP_OK;
}

// No parameters: restores active identity and any incomplete candidate separately.
// Exception: Storage failures require explicit repair, never implicit overwrite or reset.
esp_err_t loadPersistedLink()
{
    auto &settings = LUNASHELL::UserSettingsService::instance();
    esp_err_t error = settings.isInitialized() ? ESP_OK : settings.initialize();
    if (error == ESP_OK) {
        error = readLinkRecord(kPairingSetting, s_persisted_link, s_link_present);
    }
    if (error == ESP_OK) {
        error = readLinkRecord(kPendingPairingSetting, s_candidate_link, s_candidate_present);
    }
    if (error == ESP_OK && s_link_present) {
        error = encodeHex(s_persisted_link.pc_id, sizeof(s_persisted_link.pc_id),
                          s_link_snapshot.paired_pc_id, sizeof(s_link_snapshot.paired_pc_id));
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "CCP retained-state load failed; explicit repair required (%s)", esp_err_to_name(error));
    } else {
        ESP_LOGI(kTag, "CCP records restored: retained=%u pending_confirmation=%u; codes remain closed",
                 s_link_present, s_candidate_present);
    }
    return error;
}

// key: Active or pending CCP record; never an unrelated user setting.
// record: Fully formed versioned record whose CRC is already assigned.
// Exception: Latches the original write/readback error until an explicit successful Forget.
esp_err_t persistLink(const char *key, const PersistedLink &record)
{
    esp_err_t error = LUNASHELL::UserSettingsService::instance().setBlob(key, &record, sizeof(record));
    PersistedLink readback{};
    bool present = false;
    if (error == ESP_OK) {
        error = readLinkRecord(key, readback, present);
    }
    if (error == ESP_OK && (!present || std::memcmp(&record, &readback, sizeof(record)) != 0)) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    wipe(&readback, sizeof(readback));
    if (error != ESP_OK) {
        // A failed commit/readback leaves disk ownership ambiguous even when RAM
        // still contains the previous record. Only explicit repair can retire it.
        s_storage_error = error;
        ESP_LOGE(kTag, "CCP record '%s' persistence failed (%s)", key, esp_err_to_name(error));
    }
    return error;
}

// key: One CCP record removed by confirmation, cancellation or explicit Forget.
// Exception: Latches erase/commit/readback failures; absence must be independently verified.
esp_err_t eraseLinkRecord(const char *key)
{
    auto &settings = LUNASHELL::UserSettingsService::instance();
    esp_err_t error = settings.isInitialized() ? ESP_OK : settings.initialize();
    if (error == ESP_OK) {
        error = settings.eraseKey(key);
    }
    if (error == ESP_OK || error == ESP_ERR_NVS_NOT_FOUND) {
        // NVS NOT_FOUND is accepted only after readback independently confirms absence.
        std::vector<uint8_t> remaining;
        const esp_err_t read_error = settings.getBlob(key, &remaining);
        wipe(remaining.data(), remaining.size());
        error = read_error == ESP_ERR_NVS_NOT_FOUND ? ESP_OK :
            read_error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : read_error;
    }
    if (error != ESP_OK) {
        s_storage_error = error;
        ESP_LOGE(kTag, "CCP record '%s' explicit removal failed (%s)", key, esp_err_to_name(error));
    }
    return error;
}

// No parameters: caller holds s_mutex after an authenticated CONFIRM or candidate READY.
// Exception: Partial persistence stays an error and keeps recovery state available.
esp_err_t commitCandidateLocked()
{
    if (!s_candidate_present) {
        ESP_LOGE(kTag, "CCP candidate commit rejected: no candidate (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    // PC possession is now proven. A reset between this write and pending-record
    // removal can safely repeat the same authenticated completion after reboot.
    esp_err_t error = persistLink(kPairingSetting, s_candidate_link);
    if (error == ESP_OK) {
        s_persisted_link = s_candidate_link;
        s_link_present = true;
        error = encodeHex(s_persisted_link.pc_id, sizeof(s_persisted_link.pc_id),
                          s_link_snapshot.paired_pc_id, sizeof(s_link_snapshot.paired_pc_id));
    }
    if (error == ESP_OK) {
        error = eraseLinkRecord(kPendingPairingSetting);
    }
    if (error == ESP_OK) {
        wipe(&s_candidate_link, sizeof(s_candidate_link));
        s_candidate_present = false;
        ESP_LOGI(kTag, "CCP candidate committed after authenticated PC confirmation");
    }
    return error;
}

// payload: Complete bounded printable-ASCII datagram.
// destination: Exact observed peer or IPv4 broadcast destination.
// port: Destination UDP port in host byte order.
// operation: Non-secret operation name for failure logs.
// Exception: Returns ESP_ERR_INVALID_SIZE or the actual socket admission failure.
esp_err_t sendDatagram(
    const char *payload,
    const in_addr &destination,
    uint16_t port,
    const char *operation
)
{
    const size_t size = payload == nullptr ? 0 : std::strlen(payload);
    if (size == 0 || size > kMaximumDatagramBytes) {
        return ESP_ERR_INVALID_SIZE;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr = destination;
    const int sent = sendto(
        s_socket,
        payload,
        size,
        0,
        reinterpret_cast<const sockaddr *>(&address),
        sizeof(address)
    );
    if (sent != static_cast<int>(size)) {
        const int socket_error = errno;
        ESP_LOGE(kTag, "%s send failed: errno=%d", operation, socket_error);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// request_id: Sixteen lowercase hexadecimal request characters.
// Exception: Rejects every non-canonical DISCOVER layout.
bool parseDiscover(const char *payload, char request_id[17])
{
    int consumed = 0;
    const int fields = std::sscanf(
        payload,
        "CCP1|DISCOVER|request=%16[0-9a-f]%n",
        request_id,
        &consumed
    );
    return fields == 1 && std::strlen(request_id) == 16 &&
        consumed == static_cast<int>(std::strlen(payload));
}

struct PairRequest {
    char device_id[13]{};
    char session_id[9]{};
    char pc_id[33]{};
    char nonce[33]{};
    char proof[65]{};
};

// payload: Candidate canonical PAIR datagram.
// request: Receives validated-width lowercase fields.
// Exception: Rejects missing, extra, duplicated, uppercase or reordered fields.
bool parsePair(const char *payload, PairRequest &request)
{
    int consumed = 0;
    const int fields = std::sscanf(
        payload,
        "CCP1|PAIR|device=%12[0-9a-f]|session=%8[0-9a-f]|pc=%32[0-9a-f]"
        "|port=42118|nonce=%32[0-9a-f]|proof=%64[0-9a-f]%n",
        request.device_id,
        request.session_id,
        request.pc_id,
        request.nonce,
        request.proof,
        &consumed
    );
    return fields == 5 && std::strlen(request.device_id) == 12 &&
        std::strlen(request.session_id) == 8 &&
        std::strlen(request.pc_id) == 32 &&
        std::strlen(request.nonce) == 32 &&
        std::strlen(request.proof) == 64 &&
        consumed == static_cast<int>(std::strlen(payload));
}

// payload: Candidate canonical CONFIRM datagram.
// request: Receives validated-width lowercase fields.
// Exception: Rejects missing, extra, duplicated, uppercase or reordered fields.
bool parseConfirm(const char *payload, PairRequest &request)
{
    int consumed = 0;
    const int fields = std::sscanf(
        payload,
        "CCP1|CONFIRM|device=%12[0-9a-f]|session=%8[0-9a-f]|pc=%32[0-9a-f]"
        "|nonce=%32[0-9a-f]|proof=%64[0-9a-f]%n",
        request.device_id,
        request.session_id,
        request.pc_id,
        request.nonce,
        request.proof,
        &consumed
    );
    return fields == 5 && std::strlen(request.device_id) == 12 &&
        std::strlen(request.session_id) == 8 &&
        std::strlen(request.pc_id) == 32 &&
        std::strlen(request.nonce) == 32 &&
        std::strlen(request.proof) == 64 &&
        consumed == static_cast<int>(std::strlen(payload));
}

struct ReadyResponse {
    char device_id[13]{};
    char pc_id[33]{};
    char nonce[33]{};
    char proof[65]{};
};

// payload: Candidate canonical READY datagram.
// response: Receives validated-width lowercase fields.
// Exception: Rejects missing, extra, duplicated, uppercase or reordered fields.
bool parseReady(const char *payload, ReadyResponse &response)
{
    int consumed = 0;
    const int fields = std::sscanf(
        payload,
        "CCP1|READY|device=%12[0-9a-f]|pc=%32[0-9a-f]|nonce=%32[0-9a-f]"
        "|proof=%64[0-9a-f]%n",
        response.device_id,
        response.pc_id,
        response.nonce,
        response.proof,
        &consumed
    );
    return fields == 4 && std::strlen(response.device_id) == 12 &&
        std::strlen(response.pc_id) == 32 &&
        std::strlen(response.nonce) == 32 &&
        std::strlen(response.proof) == 64 &&
        consumed == static_cast<int>(std::strlen(payload));
}

struct ActivityResponse {
    char device_id[13]{};
    char pc_id[33]{};
    char nonce[33]{};
    char chat_id[65]{};
    char revision[17]{};
    char phase[12]{};
    char project[49]{};
    char proof[65]{};
};

// payload: Candidate fixed-order activity snapshot, with no unbounded strings.
// response: Receives exact-width identifiers and bounded ASCII metadata.
// Exception: Rejects missing, extra, duplicated, reordered or malformed fields.
bool parseActivity(const char *payload, ActivityResponse &response)
{
    int consumed = 0;
    // The no-selection sentinel is a literal hyphen. Keep it first: a trailing
    // hyphen can be interpreted as an unfinished range by the target scanf.
    const int fields = std::sscanf(payload,
        "CCP1|ACTIVITY|device=%12[0-9a-f]|pc=%32[0-9a-f]|nonce=%32[0-9a-f]"
        "|chat=%64[0-9a-f]|revision=%16[0-9a-f]|phase=%11[a-z]"
        "|project=%48[-0-9a-f]|proof=%64[0-9a-f]%n",
        response.device_id, response.pc_id, response.nonce, response.chat_id,
        response.revision, response.phase, response.project, response.proof, &consumed);
    const bool valid = fields == 8 && std::strlen(response.device_id) == 12 &&
        std::strlen(response.pc_id) == 32 && std::strlen(response.nonce) == 32 &&
        std::strlen(response.chat_id) == 64 && std::strlen(response.revision) == 16 &&
        std::strlen(response.proof) == 64 && consumed == static_cast<int>(std::strlen(payload));
    if (!valid) {
        ESP_LOGE(kTag, "CCP ACTIVITY field parsing failed: fields=%d/8 consumed=%d bytes=%u (%s)",
            fields, consumed, static_cast<unsigned>(std::strlen(payload)),
            esp_err_to_name(ESP_ERR_INVALID_ARG));
    }
    return valid;
}

// phase: Bounded protocol token, never interpreted as a storage index.
// state: Receives the canonical typed state on success.
// Exception: Returns INVALID_ARG for an unknown activity token.
esp_err_t decodeActivityState(const char *phase, LUNASHELL::CodexActivityState &state)
{
    using Activity = LUNASHELL::CodexActivityState;
    struct Entry { const char *name; Activity state; };
    constexpr Entry entries[] = {
        {"none", Activity::None}, {"active", Activity::Active}, {"working", Activity::Working},
        {"editing", Activity::Editing}, {"running", Activity::Running},
        {"ready", Activity::ResponseReady}, {"paused", Activity::Paused},
        {"interrupted", Activity::Interrupted},
        {"ended", Activity::Ended}, {"unavailable", Activity::Unavailable},
    };
    for (const auto &entry : entries) {
        if (std::strcmp(phase, entry.name) == 0) {
            state = entry.state;
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_ARG;
}

// No parameters: starts a fresh five-minute display-code window from canonical Wi-Fi identity.
// Exception: Publishes original candidate-removal, snapshot, identity or crypto failures.
void startPairing()
{
    esp_err_t error = ESP_OK;
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (s_candidate_present) {
            // Only an explicitly confirmed replacement can retire this candidate.
            error = eraseLinkRecord(kPendingPairingSetting);
            if (error == ESP_OK) {
                wipe(&s_candidate_link, sizeof(s_candidate_link));
                s_candidate_present = false;
            }
        }
    }
    LUNASHELL::ConnectivityObservation observation{};
    if (error == ESP_OK) {
        error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    }
    char device_id[13]{};
    if (error == ESP_OK && !wifiOnline(observation)) {
        error = ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "CCP pairing requires a valid canonical Wi-Fi connection (%s)",
                 esp_err_to_name(error));
    }
    if (error == ESP_OK) {
        error = formatDeviceId(observation.radios.wifi.mac_address, device_id);
    }
    if (error == ESP_OK) {
        ESP_LOGI(kTag, "CCP code preparation: canonical LAN and device identity admitted");
    }
    char code[7]{};
    uint8_t salt[16]{};
    uint8_t verifier[32]{};
    uint32_t session_id = 0;
    if (error == ESP_OK) {
        for (size_t index = 0; index < 6; ++index) {
            code[index] = kCodeAlphabet[esp_random() & 31U];
        }
        esp_fill_random(salt, sizeof(salt));
        session_id = esp_random();
        ESP_LOGI(kTag, "CCP code preparation: ephemeral code, salt and session generated; values withheld");
        const int64_t derivation_started_us = esp_timer_get_time();
        ESP_LOGI(kTag, "CCP verifier derivation started: core=%ld priority=%lu iterations=%lu",
            static_cast<long>(xPortGetCoreID()), static_cast<unsigned long>(kWorkerPriority),
            static_cast<unsigned long>(kPbkdf2Iterations));
        error = deriveVerifier(code, salt, verifier);
        ESP_LOGI(kTag, "CCP verifier derivation completed: elapsed_us=%lld result=%s",
            static_cast<long long>(esp_timer_get_time() - derivation_started_us), esp_err_to_name(error));
    }
    std::lock_guard<std::mutex> guard(s_mutex);
    s_pending = Command::None;
    clearPairingSecretsLocked();
    if (error != ESP_OK) {
        publishLocked(State::Failed, error);
        wipe(code, sizeof(code));
        wipe(salt, sizeof(salt));
        wipe(verifier, sizeof(verifier));
        return;
    }
    std::memcpy(s_pairing_code, code, sizeof(code));
    std::memcpy(s_pairing_salt, salt, sizeof(salt));
    std::memcpy(s_pairing_verifier, verifier, sizeof(verifier));
    s_pairing_session_id = session_id;
    std::memcpy(s_link_snapshot.device_id, device_id, sizeof(device_id));
    s_link_snapshot.deadline_at_us = esp_timer_get_time() + kPairingWindowUs;
    s_link_snapshot.ready_at_us = 0;
    s_link_snapshot.peer_ip_address[0] = '\0';
    publishLocked(State::Pairing, ESP_OK);
    ESP_LOGI(kTag, "CCP code preparation complete: display code ready, five-minute window active");
    wipe(code, sizeof(code));
    wipe(salt, sizeof(salt));
    wipe(verifier, sizeof(verifier));
}

// request_id: Exact discovery request received from one PC.
// source: Observed source address and ephemeral response port.
// Exception: Returns the HMAC, encoding or UDP send failure.
esp_err_t sendOffer(const char *request_id, const sockaddr_in &source)
{
    char device_id[13]{};
    char session_id[9]{};
    char salt_hex[33]{};
    uint8_t verifier[32]{};
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (s_link_snapshot.state != State::Pairing ||
            esp_timer_get_time() >= s_link_snapshot.deadline_at_us) {
            return ESP_ERR_INVALID_STATE;
        }
        std::memcpy(device_id, s_link_snapshot.device_id, sizeof(device_id));
        std::snprintf(session_id, sizeof(session_id), "%08lx",
                      static_cast<unsigned long>(s_pairing_session_id));
        std::memcpy(verifier, s_pairing_verifier, sizeof(verifier));
        const esp_err_t salt_error = encodeHex(
            s_pairing_salt,
            sizeof(s_pairing_salt),
            salt_hex,
            sizeof(salt_hex)
        );
        if (salt_error != ESP_OK) {
            wipe(verifier, sizeof(verifier));
            return salt_error;
        }
    }
    char proof_input[128]{};
    const int input_length = std::snprintf(
        proof_input,
        sizeof(proof_input),
        "offer|%s|%s|%s|%s",
        request_id,
        device_id,
        session_id,
        salt_hex
    );
    uint8_t proof[32]{};
    esp_err_t error = input_length > 0 && static_cast<size_t>(input_length) < sizeof(proof_input)
        ? hmacSha256(verifier, sizeof(verifier), proof_input, proof)
        : ESP_ERR_INVALID_SIZE;
    char proof_hex[65]{};
    if (error == ESP_OK) {
        error = encodeHex(proof, sizeof(proof), proof_hex, sizeof(proof_hex));
    }
    char response[kMaximumDatagramBytes + 1]{};
    const int response_length = error == ESP_OK
        ? std::snprintf(
            response,
            sizeof(response),
            "%s|OFFER|request=%s|device=%s|session=%s|salt=%s|tag=%s",
            kProtocol,
            request_id,
            device_id,
            session_id,
            salt_hex,
            proof_hex
        )
        : -1;
    if (error == ESP_OK &&
        (response_length <= 0 || static_cast<size_t>(response_length) >= sizeof(response))) {
        error = ESP_ERR_INVALID_SIZE;
    }
    if (error == ESP_OK) {
        error = sendDatagram(response, source.sin_addr, ntohs(source.sin_port), "CCP OFFER");
    }
    wipe(verifier, sizeof(verifier));
    wipe(proof, sizeof(proof));
    wipe(proof_input, sizeof(proof_input));
    wipe(proof_hex, sizeof(proof_hex));
    return error;
}

// request: Canonical PAIR request matching the active device/session identity.
// source: Observed sender address and ephemeral reply port.
// Exception: Returns proof, persistence or confirmation-send failures without reporting success.
esp_err_t acceptPair(const PairRequest &request, const sockaddr_in &source)
{
    char expected_device_id[13]{};
    char expected_session_id[9]{};
    uint8_t verifier[32]{};
    PersistedLink existing_record{};
    bool already_persisted = false;
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (s_pending != Command::None || s_link_snapshot.state != State::Pairing ||
            esp_timer_get_time() >= s_link_snapshot.deadline_at_us) {
            return ESP_ERR_INVALID_STATE;
        }
        if (s_candidate_present &&
            (std::strcmp(s_pairing_pc_id, request.pc_id) != 0 ||
             std::strcmp(s_pairing_nonce, request.nonce) != 0)) {
            ESP_LOGE(kTag, "CCP PAIR rejected: an existing candidate owns this window; cancel or replace explicitly");
            return ESP_ERR_INVALID_STATE;
        }
        std::memcpy(expected_device_id, s_link_snapshot.device_id, sizeof(expected_device_id));
        std::snprintf(expected_session_id, sizeof(expected_session_id), "%08lx",
                      static_cast<unsigned long>(s_pairing_session_id));
        std::memcpy(verifier, s_pairing_verifier, sizeof(verifier));
        already_persisted = s_candidate_present &&
            std::strcmp(s_pairing_pc_id, request.pc_id) == 0 &&
            std::strcmp(s_pairing_nonce, request.nonce) == 0;
        if (already_persisted) {
            existing_record = s_candidate_link;
        }
    }
    if (std::strcmp(request.device_id, expected_device_id) != 0 ||
        std::strcmp(request.session_id, expected_session_id) != 0) {
        wipe(verifier, sizeof(verifier));
        wipe(&existing_record, sizeof(existing_record));
        return ESP_ERR_INVALID_STATE;
    }
    char pair_input[160]{};
    const int pair_length = std::snprintf(
        pair_input,
        sizeof(pair_input),
        "pair|%s|%s|%s|42118|%s",
        request.device_id,
        request.session_id,
        request.pc_id,
        request.nonce
    );
    uint8_t expected_proof[32]{};
    uint8_t received_proof[32]{};
    esp_err_t error = pair_length > 0 && static_cast<size_t>(pair_length) < sizeof(pair_input)
        ? hmacSha256(verifier, sizeof(verifier), pair_input, expected_proof)
        : ESP_ERR_INVALID_SIZE;
    if (error == ESP_OK) {
        error = decodeHex(request.proof, received_proof, sizeof(received_proof));
    }
    if (error == ESP_OK &&
        !constantTimeEqual(expected_proof, received_proof, sizeof(expected_proof))) {
        error = ESP_ERR_INVALID_CRC;
        ESP_LOGE(kTag, "CCP PAIR proof rejected (%s)", esp_err_to_name(error));
    }
    if (error != ESP_OK) {
        wipe(verifier, sizeof(verifier));
        wipe(expected_proof, sizeof(expected_proof));
        wipe(received_proof, sizeof(received_proof));
        wipe(pair_input, sizeof(pair_input));
        wipe(&existing_record, sizeof(existing_record));
        if (error != ESP_ERR_INVALID_CRC && error != ESP_ERR_INVALID_ARG) {
            std::lock_guard<std::mutex> guard(s_mutex);
            clearPairingSecretsLocked();
            publishLocked(State::Failed, error);
        }
        return error;
    }
    char link_input[160]{};
    const int link_length = !already_persisted
        ? std::snprintf(
            link_input,
            sizeof(link_input),
            "link|%s|%s|%s|%s",
            request.device_id,
            request.session_id,
            request.pc_id,
            request.nonce
        )
        : -1;
    uint8_t link_key[32]{};
    if (!already_persisted) {
        error = link_length > 0 && static_cast<size_t>(link_length) < sizeof(link_input)
            ? hmacSha256(verifier, sizeof(verifier), link_input, link_key)
            : ESP_ERR_INVALID_SIZE;
    } else {
        std::memcpy(link_key, existing_record.link_key, sizeof(link_key));
    }
    PersistedLink record{};
    bool record_persisted = already_persisted;
    if (already_persisted) {
        record = existing_record;
    } else if (error == ESP_OK) {
        error = decodeHex(request.pc_id, record.pc_id, sizeof(record.pc_id));
    }
    if (error == ESP_OK && !already_persisted) {
        std::memcpy(record.link_key, link_key, sizeof(link_key));
        record.crc32 = persistedLinkCrc32(record);
        std::lock_guard<std::mutex> guard(s_mutex);
        if (s_pending != Command::None || s_link_snapshot.state != State::Pairing) {
            error = ESP_ERR_INVALID_STATE;
        } else {
            // Keep the active association until the PC proves it saved this candidate.
            error = persistLink(kPendingPairingSetting, record);
            record_persisted = error == ESP_OK;
            if (record_persisted) {
                s_candidate_link = record;
                s_candidate_present = true;
                std::memcpy(s_pairing_pc_id, request.pc_id, sizeof(s_pairing_pc_id));
                std::memcpy(s_pairing_nonce, request.nonce, sizeof(s_pairing_nonce));
            }
        }
    }
    char confirmation_input[160]{};
    const int confirmation_length = error == ESP_OK
        ? std::snprintf(
            confirmation_input,
            sizeof(confirmation_input),
            "paired|%s|%s|%s|%s",
            request.device_id,
            request.session_id,
            request.pc_id,
            request.nonce
        )
        : -1;
    uint8_t confirmation_proof[32]{};
    if (error == ESP_OK) {
        error = confirmation_length > 0 &&
            static_cast<size_t>(confirmation_length) < sizeof(confirmation_input)
            ? hmacSha256(link_key, sizeof(link_key), confirmation_input, confirmation_proof)
            : ESP_ERR_INVALID_SIZE;
    }
    char confirmation_hex[65]{};
    if (error == ESP_OK) {
        error = encodeHex(
            confirmation_proof,
            sizeof(confirmation_proof),
            confirmation_hex,
            sizeof(confirmation_hex)
        );
    }
    char response[kMaximumDatagramBytes + 1]{};
    const int response_length = error == ESP_OK
        ? std::snprintf(
            response,
            sizeof(response),
            "%s|PAIRED|device=%s|session=%s|pc=%s|nonce=%s|proof=%s",
            kProtocol,
            request.device_id,
            request.session_id,
            request.pc_id,
            request.nonce,
            confirmation_hex
        )
        : -1;
    if (error == ESP_OK &&
        (response_length <= 0 || static_cast<size_t>(response_length) >= sizeof(response))) {
        error = ESP_ERR_INVALID_SIZE;
    }
    if (error == ESP_OK) {
        bool sent = false;
        for (uint8_t attempt = 0; attempt < 3; ++attempt) {
            sent = sendDatagram(
                response,
                source.sin_addr,
                ntohs(source.sin_port),
                "CCP PAIRED"
            ) == ESP_OK || sent;
        }
        error = sent ? ESP_OK : ESP_FAIL;
    }
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (s_pending == Command::None && s_link_snapshot.state == State::Pairing) {
            if (record_persisted) {
                // PAIRED proves candidate durability, not an active association.
                publishLocked(State::Pairing, error);
            } else {
                clearPairingSecretsLocked();
                publishLocked(State::Failed, error);
            }
        }
    }
    wipe(verifier, sizeof(verifier));
    wipe(expected_proof, sizeof(expected_proof));
    wipe(received_proof, sizeof(received_proof));
    wipe(link_key, sizeof(link_key));
    wipe(confirmation_proof, sizeof(confirmation_proof));
    wipe(&record, sizeof(record));
    wipe(&existing_record, sizeof(existing_record));
    wipe(pair_input, sizeof(pair_input));
    wipe(link_input, sizeof(link_input));
    wipe(confirmation_input, sizeof(confirmation_input));
    wipe(confirmation_hex, sizeof(confirmation_hex));
    return error;
}

// request: CONFIRM proof sent only after CCP protected its local link record.
// source: Observed plugin address and UDP port.
// Exception: Returns identity, proof, persistence or ACTIVE-send failures without inventing completion.
esp_err_t acceptConfirm(const PairRequest &request, const sockaddr_in &source)
{
    char expected_device_id[13]{};
    char expected_session_id[9]{};
    char expected_pc_id[33]{};
    char expected_nonce[33]{};
    uint8_t link_key[32]{};
    State accepted_state = State::Idle;
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        const int64_t now_us = esp_timer_get_time();
        accepted_state = s_link_snapshot.state;
        const bool current_pairing = accepted_state == State::Pairing &&
            now_us < s_link_snapshot.deadline_at_us;
        const bool activation_replay =
            (accepted_state == State::Paired || accepted_state == State::Discovering ||
             accepted_state == State::Connected) &&
            now_us < s_activation_replay_until_us;
        if (s_pending != Command::None || (!current_pairing && !activation_replay) ||
            (current_pairing && !s_candidate_present && !s_link_present) ||
            (activation_replay && !s_link_present)) {
            return ESP_ERR_INVALID_STATE;
        }
        std::memcpy(expected_device_id, s_link_snapshot.device_id, sizeof(expected_device_id));
        if (current_pairing) {
            std::snprintf(expected_session_id, sizeof(expected_session_id), "%08lx",
                          static_cast<unsigned long>(s_pairing_session_id));
            std::memcpy(expected_pc_id, s_pairing_pc_id, sizeof(expected_pc_id));
            std::memcpy(expected_nonce, s_pairing_nonce, sizeof(expected_nonce));
        } else {
            std::memcpy(
                expected_session_id,
                s_activation_session_id,
                sizeof(expected_session_id)
            );
            std::memcpy(expected_pc_id, s_activation_pc_id, sizeof(expected_pc_id));
            std::memcpy(expected_nonce, s_activation_nonce, sizeof(expected_nonce));
        }
        const auto &record = current_pairing && s_candidate_present ? s_candidate_link : s_persisted_link;
        std::memcpy(link_key, record.link_key, sizeof(link_key));
    }
    if (std::strcmp(request.device_id, expected_device_id) != 0 ||
        std::strcmp(request.session_id, expected_session_id) != 0 ||
        std::strcmp(request.pc_id, expected_pc_id) != 0 ||
        std::strcmp(request.nonce, expected_nonce) != 0) {
        wipe(link_key, sizeof(link_key));
        return ESP_ERR_INVALID_STATE;
    }
    char proof_input[160]{};
    const int input_length = std::snprintf(
        proof_input,
        sizeof(proof_input),
        "confirm|%s|%s|%s|%s",
        request.device_id,
        request.session_id,
        request.pc_id,
        request.nonce
    );
    uint8_t expected_proof[32]{};
    uint8_t received_proof[32]{};
    esp_err_t error = input_length > 0 && static_cast<size_t>(input_length) < sizeof(proof_input)
        ? hmacSha256(link_key, sizeof(link_key), proof_input, expected_proof)
        : ESP_ERR_INVALID_SIZE;
    if (error == ESP_OK) {
        error = decodeHex(request.proof, received_proof, sizeof(received_proof));
    }
    if (error == ESP_OK &&
        !constantTimeEqual(expected_proof, received_proof, sizeof(expected_proof))) {
        error = ESP_ERR_INVALID_CRC;
        ESP_LOGE(kTag, "CCP CONFIRM proof rejected (%s)", esp_err_to_name(error));
    }
    if (error == ESP_OK && accepted_state == State::Pairing) {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (s_pending != Command::None || s_link_snapshot.state != State::Pairing ||
            std::strcmp(s_pairing_pc_id, request.pc_id) != 0 ||
            std::strcmp(s_pairing_nonce, request.nonce) != 0) {
            error = ESP_ERR_INVALID_STATE;
        } else if (s_candidate_present) {
            error = commitCandidateLocked();
        }
    }
    char active_input[160]{};
    const int active_length = error == ESP_OK
        ? std::snprintf(
            active_input,
            sizeof(active_input),
            "active|%s|%s|%s|%s",
            request.device_id,
            request.session_id,
            request.pc_id,
            request.nonce
        )
        : -1;
    uint8_t active_proof[32]{};
    if (error == ESP_OK) {
        error = active_length > 0 && static_cast<size_t>(active_length) < sizeof(active_input)
            ? hmacSha256(link_key, sizeof(link_key), active_input, active_proof)
            : ESP_ERR_INVALID_SIZE;
    }
    char active_hex[65]{};
    if (error == ESP_OK) {
        error = encodeHex(active_proof, sizeof(active_proof), active_hex, sizeof(active_hex));
    }
    char response[kMaximumDatagramBytes + 1]{};
    const int response_length = error == ESP_OK
        ? std::snprintf(
            response,
            sizeof(response),
            "%s|ACTIVE|device=%s|session=%s|pc=%s|nonce=%s|proof=%s",
            kProtocol,
            request.device_id,
            request.session_id,
            request.pc_id,
            request.nonce,
            active_hex
        )
        : -1;
    if (error == ESP_OK &&
        (response_length <= 0 || static_cast<size_t>(response_length) >= sizeof(response))) {
        error = ESP_ERR_INVALID_SIZE;
    }
    if (error == ESP_OK) {
        bool sent = false;
        for (uint8_t attempt = 0; attempt < 3; ++attempt) {
            sent = sendDatagram(
                response,
                source.sin_addr,
                ntohs(source.sin_port),
                "CCP ACTIVE"
            ) == ESP_OK || sent;
        }
        error = sent ? ESP_OK : ESP_FAIL;
    }
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (error == ESP_OK && s_pending == Command::None && s_link_snapshot.state == accepted_state) {
            if (accepted_state == State::Pairing) {
                std::memcpy(
                    s_activation_session_id,
                    request.session_id,
                    sizeof(s_activation_session_id)
                );
                std::memcpy(
                    s_activation_pc_id,
                    request.pc_id,
                    sizeof(s_activation_pc_id)
                );
                std::memcpy(
                    s_activation_nonce,
                    request.nonce,
                    sizeof(s_activation_nonce)
                );
                s_activation_replay_until_us = esp_timer_get_time() + kActivationReplayUs;
                clearPairingSecretsLocked();
                publishLocked(State::Paired, ESP_OK);
            }
        } else if (error != ESP_OK && error != ESP_ERR_INVALID_CRC && error != ESP_ERR_INVALID_ARG &&
                   error != ESP_ERR_INVALID_STATE && s_pending == Command::None &&
                   s_link_snapshot.state == accepted_state) {
            if (s_storage_error != ESP_OK) {
                clearPairingSecretsLocked();
                clearActivationReplayLocked();
                publishLocked(State::Failed, error);
            } else {
                publishLocked(accepted_state, error);
            }
        }
    }
    wipe(link_key, sizeof(link_key));
    wipe(expected_proof, sizeof(expected_proof));
    wipe(received_proof, sizeof(received_proof));
    wipe(active_proof, sizeof(active_proof));
    wipe(proof_input, sizeof(proof_input));
    wipe(active_input, sizeof(active_input));
    wipe(active_hex, sizeof(active_hex));
    return error;
}

// wifi: Current canonical station identity admitted by the reconnect policy.
// Exception: Returns missing/retired-state, HMAC, formatting or UDP send failures.
esp_err_t sendFind(const LUNASHELL::WifiSnapshot &wifi)
{
    char device_id[13]{};
    esp_err_t error = formatDeviceId(wifi.mac_address, device_id);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "CCP discovery station identity rejected (%s)", esp_err_to_name(error));
        return error;
    }
    char pc_id[33]{};
    uint8_t link_key[32]{};
    uint8_t nonce[16]{};
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        if ((!s_link_present && !s_candidate_present) || s_pending != Command::None ||
            (s_link_snapshot.state != State::Discovering && s_link_snapshot.state != State::Connected)) {
            ESP_LOGE(kTag, "CCP FIND rejected: retained discovery is no longer active (%s)",
                     esp_err_to_name(ESP_ERR_INVALID_STATE));
            return ESP_ERR_INVALID_STATE;
        }
        std::memcpy(s_link_snapshot.device_id, device_id, sizeof(device_id));
        const auto &record = s_candidate_present ? s_candidate_link : s_persisted_link;
        error = encodeHex(record.pc_id, sizeof(record.pc_id), pc_id, sizeof(pc_id));
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "CCP discovery PC identity encoding failed (%s)", esp_err_to_name(error));
            return error;
        }
        std::memcpy(link_key, record.link_key, sizeof(link_key));
    }
    esp_fill_random(nonce, sizeof(nonce));
    char nonce_hex[33]{};
    error = encodeHex(nonce, sizeof(nonce), nonce_hex, sizeof(nonce_hex));
    char proof_input[128]{};
    const int input_length = error == ESP_OK
        ? std::snprintf(
            proof_input,
            sizeof(proof_input),
            "find-events|%s|%s|%s",
            device_id,
            pc_id,
            nonce_hex
        )
        : -1;
    uint8_t proof[32]{};
    if (error == ESP_OK) {
        error = input_length > 0 && static_cast<size_t>(input_length) < sizeof(proof_input)
            ? hmacSha256(link_key, sizeof(link_key), proof_input, proof)
            : ESP_ERR_INVALID_SIZE;
    }
    char proof_hex[65]{};
    if (error == ESP_OK) {
        error = encodeHex(proof, sizeof(proof), proof_hex, sizeof(proof_hex));
    }
    char request[kMaximumDatagramBytes + 1]{};
    const int request_length = error == ESP_OK
        ? std::snprintf(
            request,
            sizeof(request),
            "%s|FIND|device=%s|pc=%s|nonce=%s|activity=1|events=1|proof=%s",
            kProtocol,
            device_id,
            pc_id,
            nonce_hex,
            proof_hex
        )
        : -1;
    if (error == ESP_OK &&
        (request_length <= 0 || static_cast<size_t>(request_length) >= sizeof(request))) {
        error = ESP_ERR_INVALID_SIZE;
    }
    if (error == ESP_OK) {
        in_addr broadcast{};
        broadcast.s_addr = htonl(INADDR_BROADCAST);
        error = sendDatagram(request, broadcast, kPluginPort, "CCP FIND");
    }
    if (error == ESP_OK) {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (s_pending == Command::None &&
            (s_link_snapshot.state == State::Discovering || s_link_snapshot.state == State::Connected)) {
            std::memcpy(s_find_nonce, nonce, sizeof(nonce));
            s_activity_received_for_nonce = false;
            s_event_received_for_nonce = false;
            wipe(s_ready_nonce, sizeof(s_ready_nonce));
            s_next_find_at_us = esp_timer_get_time() + kFindIntervalUs;
        } else {
            error = ESP_ERR_INVALID_STATE;
            ESP_LOGW(kTag, "CCP FIND completion retired by a newer command (%s)", esp_err_to_name(error));
        }
    }
    wipe(link_key, sizeof(link_key));
    wipe(nonce, sizeof(nonce));
    wipe(proof, sizeof(proof));
    wipe(proof_input, sizeof(proof_input));
    wipe(proof_hex, sizeof(proof_hex));
    return error;
}

// No parameters: admits retained-link recovery; the worker waits for canonical LAN readiness.
// Exception: Missing retained identity or the original storage error is published as failure.
void startDiscovery()
{
    std::lock_guard<std::mutex> guard(s_mutex);
    s_pending = Command::None;
    if ((!s_link_present && !s_candidate_present) || s_storage_error != ESP_OK) {
        const esp_err_t error = s_storage_error != ESP_OK ? s_storage_error : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "CCP discovery rejected: retained=%u candidate=%u storage=%s (%s)",
                 s_link_present, s_candidate_present, esp_err_to_name(s_storage_error), esp_err_to_name(error));
        publishLocked(State::Failed, error);
        return;
    }
    s_last_ready_at_us = 0;
    s_next_find_at_us = 0;
    s_link_snapshot.ready_at_us = 0;
    s_link_snapshot.peer_ip_address[0] = '\0';
    wipe(s_find_nonce, sizeof(s_find_nonce));
    s_activity_received_for_nonce = false;
    publishLocked(State::Discovering, ESP_OK);
    ESP_LOGI(kTag, "CCP reconnect admitted with retained pairing; code window stays closed");
}

// response: Authenticated READY whose nonce still owns the current discovery request.
// source: Observed PC endpoint receiving the durable-device commit acknowledgement.
// Exception: Returns the original encoding, HMAC or transport failure; requires s_mutex ownership.
esp_err_t sendCommittedLocked(const ReadyResponse &response, const sockaddr_in &source)
{
    // A retained candidate is insufficient proof. Only the read-back active
    // record may acknowledge commit, including every retry after an ACK loss.
    if (!s_link_present || s_candidate_present) {
        return ESP_ERR_INVALID_STATE;
    }
    char proof_input[128]{};
    uint8_t proof[32]{};
    char proof_hex[65]{};
    char payload[256]{};
    const int input_length = std::snprintf(proof_input, sizeof(proof_input),
        "committed|%s|%s|%s", response.device_id, response.pc_id, response.nonce);
    esp_err_t error = input_length > 0 && static_cast<size_t>(input_length) < sizeof(proof_input)
        ? hmacSha256(s_persisted_link.link_key, sizeof(s_persisted_link.link_key), proof_input, proof)
        : ESP_ERR_INVALID_SIZE;
    if (error == ESP_OK) {
        error = encodeHex(proof, sizeof(proof), proof_hex, sizeof(proof_hex));
    }
    if (error == ESP_OK) {
        const int length = std::snprintf(payload, sizeof(payload),
            "CCP1|COMMITTED|device=%s|pc=%s|nonce=%s|proof=%s",
            response.device_id, response.pc_id, response.nonce, proof_hex);
        error = length > 0 && static_cast<size_t>(length) < sizeof(payload)
            ? sendDatagram(payload, source.sin_addr, ntohs(source.sin_port), "CCP COMMITTED")
            : ESP_ERR_INVALID_SIZE;
    }
    wipe(proof_input, sizeof(proof_input));
    wipe(proof, sizeof(proof));
    wipe(proof_hex, sizeof(proof_hex));
    wipe(payload, sizeof(payload));
    return error;
}

// response: Canonical READY message candidate.
// source: Observed responder address.
// Exception: Returns identity, proof, crypto, superseded-command, persistence or acknowledgement errors.
esp_err_t acceptReady(const ReadyResponse &response, const sockaddr_in &source)
{
    if (ntohs(source.sin_port) != kPluginPort) {
        return ESP_ERR_INVALID_ARG;
    }
    char device_id[13]{};
    char pc_id[33]{};
    char nonce_hex[33]{};
    uint8_t link_key[32]{};
    uint8_t nonce[16]{};
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        if ((!s_link_present && !s_candidate_present) || s_pending != Command::None ||
            (s_link_snapshot.state != State::Discovering &&
             s_link_snapshot.state != State::Connected)) {
            return ESP_ERR_INVALID_STATE;
        }
        std::memcpy(device_id, s_link_snapshot.device_id, sizeof(device_id));
        const auto &record = s_candidate_present ? s_candidate_link : s_persisted_link;
        const esp_err_t identity_error = encodeHex(record.pc_id, sizeof(record.pc_id), pc_id, sizeof(pc_id));
        if (identity_error != ESP_OK) {
            ESP_LOGE(kTag, "CCP READY PC identity encoding failed (%s)", esp_err_to_name(identity_error));
            return identity_error;
        }
        std::memcpy(link_key, record.link_key, sizeof(link_key));
        std::memcpy(nonce, s_find_nonce, sizeof(nonce));
    }
    esp_err_t error = encodeHex(nonce, sizeof(nonce), nonce_hex, sizeof(nonce_hex));
    if (error == ESP_OK &&
        (std::strcmp(response.device_id, device_id) != 0 ||
         std::strcmp(response.pc_id, pc_id) != 0 ||
         std::strcmp(response.nonce, nonce_hex) != 0)) {
        error = ESP_ERR_INVALID_STATE;
    }
    char proof_input[128]{};
    const int input_length = error == ESP_OK
        ? std::snprintf(
            proof_input,
            sizeof(proof_input),
            "ready|%s|%s|%s",
            device_id,
            pc_id,
            nonce_hex
        )
        : -1;
    uint8_t expected_proof[32]{};
    uint8_t received_proof[32]{};
    if (error == ESP_OK) {
        error = input_length > 0 && static_cast<size_t>(input_length) < sizeof(proof_input)
            ? hmacSha256(link_key, sizeof(link_key), proof_input, expected_proof)
            : ESP_ERR_INVALID_SIZE;
    }
    if (error == ESP_OK) {
        error = decodeHex(response.proof, received_proof, sizeof(received_proof));
    }
    if (error == ESP_OK &&
        !constantTimeEqual(expected_proof, received_proof, sizeof(expected_proof))) {
        error = ESP_ERR_INVALID_CRC;
    }
    if (error == ESP_OK) {
        char peer_address[16]{};
        if (inet_ntop(AF_INET, &source.sin_addr, peer_address, sizeof(peer_address)) == nullptr) {
            error = ESP_FAIL;
        } else {
            std::lock_guard<std::mutex> guard(s_mutex);
            // Proof computation releases the lock. A newer user command owns
            // the state even if this older READY was cryptographically valid.
            if (s_pending != Command::None ||
                (s_link_snapshot.state != State::Discovering && s_link_snapshot.state != State::Connected) ||
                !constantTimeEqual(nonce, s_find_nonce, sizeof(nonce))) {
                error = ESP_ERR_INVALID_STATE;
            } else {
                if (s_candidate_present) {
                    // A fresh candidate-key READY proves the PC retained its side
                    // even when a reboot interrupted the original CONFIRM exchange.
                    error = commitCandidateLocked();
                }
                if (error == ESP_OK) {
                    s_last_ready_at_us = esp_timer_get_time();
                    std::memcpy(s_ready_nonce, response.nonce, sizeof(s_ready_nonce));
                    s_link_snapshot.ready_at_us = s_last_ready_at_us;
                    std::memcpy(s_link_snapshot.peer_ip_address, peer_address,
                                sizeof(s_link_snapshot.peer_ip_address));
                    error = sendCommittedLocked(response, source);
                    // READY still proves liveness if the outbound ACK fails;
                    // retain the real error and retry COMMITTED on the next READY.
                    publishLocked(State::Connected, error);
                } else {
                    publishLocked(State::Failed, error);
                }
            }
        }
    }
    wipe(link_key, sizeof(link_key));
    wipe(nonce, sizeof(nonce));
    wipe(expected_proof, sizeof(expected_proof));
    wipe(received_proof, sizeof(received_proof));
    wipe(proof_input, sizeof(proof_input));
    return error;
}

// response: Bounded, parsed candidate activity from the paired PC.
// Exception: Returns the original identity, replay, HMAC, encoding or state error.
esp_err_t acceptActivity(const ActivityResponse &response)
{
    std::lock_guard<std::mutex> guard(s_mutex);
    if (!s_link_present || s_activity_received_for_nonce ||
        (s_link_snapshot.state != State::Connected && s_link_snapshot.state != State::Discovering)) {
        return ESP_ERR_INVALID_STATE;
    }
    char nonce[33]{};
    esp_err_t error = encodeHex(s_find_nonce, sizeof(s_find_nonce), nonce, sizeof(nonce));
    if (error == ESP_OK && (std::strcmp(response.device_id, s_link_snapshot.device_id) != 0 ||
        std::strcmp(response.pc_id, s_link_snapshot.paired_pc_id) != 0 ||
        std::strcmp(response.nonce, nonce) != 0)) {
        error = ESP_ERR_INVALID_STATE;
    }
    char proof_input[384]{};
    const int length = std::snprintf(proof_input, sizeof(proof_input),
        "activity|%s|%s|%s|%s|%s|%s|%s", response.device_id, response.pc_id,
        response.nonce, response.chat_id, response.revision, response.phase, response.project);
    uint8_t expected[32]{};
    uint8_t received[32]{};
    if (error == ESP_OK) {
        error = length > 0 && static_cast<size_t>(length) < sizeof(proof_input)
            ? hmacSha256(s_persisted_link.link_key, sizeof(s_persisted_link.link_key), proof_input, expected)
            : ESP_ERR_INVALID_SIZE;
    }
    if (error == ESP_OK) {
        error = decodeHex(response.proof, received, sizeof(received));
    }
    if (error == ESP_OK && !constantTimeEqual(expected, received, sizeof(expected))) {
        error = ESP_ERR_INVALID_CRC;
    }
    LUNASHELL::CodexActivitySnapshot activity{};
    if (error == ESP_OK) {
        error = decodeActivityState(response.phase, activity.state);
    }
    uint8_t revision[8]{};
    if (error == ESP_OK) {
        error = decodeHex(response.revision, revision, sizeof(revision));
    }
    if (error == ESP_OK) {
        const bool no_chat = activity.state == LUNASHELL::CodexActivityState::None;
        const bool zero_chat = std::strspn(response.chat_id, "0") == 64;
        const size_t project_size = std::strlen(response.project);
        if (no_chat) {
            if (!zero_chat || std::strcmp(response.project, "-") != 0) {
                error = ESP_ERR_INVALID_ARG;
            }
        } else if (zero_chat || project_size < 2 || project_size % 2 != 0) {
            error = ESP_ERR_INVALID_ARG;
        } else {
            error = decodeHex(response.project, reinterpret_cast<uint8_t *>(activity.project), project_size / 2);
            for (size_t index = 0; error == ESP_OK && index < project_size / 2; ++index) {
                if (activity.project[index] < 32 || activity.project[index] > 126) {
                    error = ESP_ERR_INVALID_ARG;
                }
            }
        }
    }
    if (error == ESP_OK) {
        for (const uint8_t value : revision) {
            activity.revision = (activity.revision << 8U) | value;
        }
        std::memcpy(activity.chat_id, response.chat_id, sizeof(activity.chat_id));
        activity.data_valid = activity.state != LUNASHELL::CodexActivityState::Unavailable;
        activity.data_stale = !activity.data_valid;
        activity.last_error = activity.data_valid ? ESP_OK : ESP_ERR_TIMEOUT;
        activity.health = activity.data_valid ? LUNASHELL::DeviceHealth::Ready : LUNASHELL::DeviceHealth::Degraded;
        activity.last_attempt_at_us = esp_timer_get_time();
        activity.last_success_at_us = activity.data_valid ? activity.last_attempt_at_us :
            (std::strcmp(activity.chat_id, s_link_snapshot.activity.chat_id) == 0
                ? s_link_snapshot.activity.last_success_at_us : 0);
        const bool transition = s_link_snapshot.activity.last_attempt_at_us == 0 ||
            s_link_snapshot.activity.state != activity.state ||
            std::strcmp(s_link_snapshot.activity.chat_id, activity.chat_id) != 0;
        s_link_snapshot.activity = activity;
        LUNASHELL::DeviceSnapshotService::instance().reportCodexLink(s_link_snapshot);
        s_activity_received_for_nonce = true;
        if (transition) {
            ESP_LOGI(kTag, "CCP ACTIVITY authenticated: state=%u selected=%s",
                static_cast<unsigned>(activity.state),
                activity.state == LUNASHELL::CodexActivityState::None ? "no" : "yes");
        }
    }
    wipe(expected, sizeof(expected));
    wipe(received, sizeof(received));
    wipe(proof_input, sizeof(proof_input));
    return error;
}

// No parameters: caller owns s_mutex; failed publication remains queued for the owner.
// Exception: Returns the real snapshot update failure without claiming delivery.
esp_err_t publishEventLocked()
{
    const esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().reportCodexEvent(s_event_snapshot);
    s_event_publication_pending = error != ESP_OK;
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "CCP event publication failed (%s)", esp_err_to_name(error));
    }
    return error;
}

// state: Local transaction state, independent of activity or the connection lease.
bool eventIsOpen(LUNASHELL::CodexEventState state)
{
    using EventState = LUNASHELL::CodexEventState;
    return state == EventState::Pending || state == EventState::Sending || state == EventState::Received;
}

// value: Complete canonical unsigned decimal value from a signed packet.
// seconds: Receives the bounded remaining lifetime; no UTC clock is assumed.
bool parseEventTtl(const char *value, uint16_t &seconds)
{
    const size_t length = std::strlen(value);
    if (length == 0 || length > 3 || (length > 1 && value[0] == '0')) {
        return false;
    }
    seconds = 0;
    for (size_t index = 0; index < length; ++index) {
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
        seconds = static_cast<uint16_t>(seconds * 10U + static_cast<unsigned>(value[index] - '0'));
    }
    return seconds <= 180;
}

// payload: Worker-owned datagram copied into shared worker scratch for tokenization.
// prefix: Exact packet verb and trailing separator.
// names: Canonical field names in wire order; duplicate/unknown fields cannot be skipped.
// count: Exact required number of fields.
// values: Receives pointers valid only until the worker parses its next event packet.
bool parseEventFields(const char *payload, const char *prefix, const char *const *names,
                      size_t count, char **values)
{
    const size_t size = std::strlen(payload);
    const size_t prefix_size = std::strlen(prefix);
    if (size > kMaximumEventDatagramBytes || std::strncmp(payload, prefix, prefix_size) != 0) {
        return false;
    }
    std::memcpy(s_event_parse_buffer, payload, size + 1);
    char *cursor = s_event_parse_buffer + prefix_size;
    for (size_t index = 0; index < count; ++index) {
        const size_t name_size = std::strlen(names[index]);
        if (std::strncmp(cursor, names[index], name_size) != 0 || cursor[name_size] != '=') {
            return false;
        }
        values[index] = cursor + name_size + 1;
        char *separator = std::strchr(values[index], '|');
        if ((index + 1 < count) != (separator != nullptr)) {
            return false;
        }
        if (separator != nullptr) {
            *separator = '\0';
            cursor = separator + 1;
        }
        if (values[index][0] == '\0') {
            return false;
        }
    }
    return true;
}

// source: Datagram endpoint; only the authenticated current READY address is admitted.
// device: Exact device identity in the envelope.
// pc: Exact retained PC identity in the envelope.
// nonce: Current poll nonce for EVENT, or last response nonce for REPLYACK.
// reply_ack: Selects the nonce owner without permitting cross-domain replay.
// Exception: Returns state/identity/endpoint errors; caller owns s_mutex.
esp_err_t admitEventEnvelopeLocked(const sockaddr_in &source, const char *device, const char *pc,
                                  const char *nonce, bool reply_ack)
{
    if (!s_link_present || s_candidate_present || s_pending != Command::None ||
        s_link_snapshot.state != State::Connected || ntohs(source.sin_port) != kPluginPort ||
        std::strcmp(device, s_link_snapshot.device_id) != 0 ||
        std::strcmp(pc, s_link_snapshot.paired_pc_id) != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    char address[16]{};
    if (inet_ntop(AF_INET, &source.sin_addr, address, sizeof(address)) == nullptr) {
        return ESP_FAIL;
    }
    if (std::strcmp(address, s_link_snapshot.peer_ip_address) != 0 ||
        std::strlen(nonce) != 32 || std::strcmp(nonce, reply_ack ? s_reply_nonce : s_ready_nonce) != 0 ||
        (!reply_ack && s_event_received_for_nonce)) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

// proof: Lowercase authentication tag; the canonical signed input is in worker scratch.
// Exception: Returns the original crypto error or INVALID_CRC for a mismatched proof.
esp_err_t verifyEventProofLocked(const char *proof)
{
    uint8_t expected[32]{};
    uint8_t received[32]{};
    esp_err_t error = decodeHex(proof, received, sizeof(received));
    if (error == ESP_OK) {
        error = hmacSha256(s_persisted_link.link_key, sizeof(s_persisted_link.link_key),
                          s_event_proof_input, expected);
    }
    if (error == ESP_OK && !constantTimeEqual(expected, received, sizeof(expected))) {
        error = ESP_ERR_INVALID_CRC;
    }
    wipe(expected, sizeof(expected));
    wipe(received, sizeof(received));
    wipe(s_event_proof_input, sizeof(s_event_proof_input));
    return error;
}

// left: Previously authenticated immutable request body.
// right: Candidate snapshot; selected_id alone may change during response delivery.
bool sameEventContent(const LUNASHELL::CodexEventPayload &left,
                      const LUNASHELL::CodexEventPayload &right)
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

// value: Exact state token from an authenticated EVENT frame.
// state: Receives a wire-supported state; local Sending is never accepted from the PC.
bool decodeEventState(const char *value, LUNASHELL::CodexEventState &state)
{
    using EventState = LUNASHELL::CodexEventState;
    struct Entry { const char *name; EventState state; };
    static constexpr Entry entries[] = {
        {"none", EventState::None}, {"pending", EventState::Pending},
        {"received", EventState::Received}, {"delivered", EventState::Delivered},
        {"cancelled", EventState::Cancelled}, {"expired", EventState::Expired},
    };
    for (const auto &entry : entries) {
        if (std::strcmp(value, entry.name) == 0) {
            state = entry.state;
            return true;
        }
    }
    return false;
}

// payload: Complete bounded EVENT envelope, never logged as raw user content.
// source: Observed UDP endpoint matched against authenticated READY.
// Exception: Returns precise envelope/schema/replay/publication/wake errors.
esp_err_t acceptEvent(const char *payload, const sockaddr_in &source)
{
    static constexpr const char *names[] = {"device", "pc", "nonce", "id", "state", "ttl", "data", "proof"};
    char *values[8]{};
    if (!parseEventFields(payload, "CCP1|EVENT|", names, 8, values)) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> guard(s_mutex);
    esp_err_t error = admitEventEnvelopeLocked(source, values[0], values[1], values[2], false);
    if (error != ESP_OK) {
        return error;
    }
    const int length = std::snprintf(s_event_proof_input, sizeof(s_event_proof_input),
        "event|%s|%s|%s|%s|%s|%s|%s", values[0], values[1], values[2], values[3], values[4], values[5], values[6]);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(s_event_proof_input)) {
        return ESP_ERR_INVALID_SIZE;
    }
    error = verifyEventProofLocked(values[7]);
    if (error != ESP_OK) {
        return error;
    }
    uint8_t request_bytes[16]{};
    uint16_t ttl = 0;
    LUNASHELL::CodexEventState state{};
    if (decodeHex(values[3], request_bytes, sizeof(request_bytes)) != ESP_OK ||
        !parseEventTtl(values[5], ttl) || !decodeEventState(values[4], state)) {
        return ESP_ERR_INVALID_ARG;
    }
    using EventState = LUNASHELL::CodexEventState;
    const int64_t now = esp_timer_get_time();
    if (state == EventState::None) {
        if (!allZero(request_bytes, sizeof(request_bytes)) || ttl != 0 || std::strcmp(values[6], "-") != 0) {
            return ESP_ERR_INVALID_ARG;
        }
        s_event_received_for_nonce = true;
        if (eventIsOpen(s_event_snapshot.state)) {
            // A restarted recipient has lost the volatile ticket. Preserve its
            // question on screen as unavailable, never invent a cancellation or answer.
            s_event_snapshot.state = EventState::Rejected;
            s_event_snapshot.last_error = ESP_ERR_NOT_FOUND;
            s_event_snapshot.data_valid = false;
            s_event_snapshot.data_stale = true;
            s_event_snapshot.health = LUNASHELL::DeviceHealth::Degraded;
            s_reply_pending = false;
            ++s_event_snapshot.revision;
            return publishEventLocked();
        }
        return ESP_OK;
    }
    if (allZero(request_bytes, sizeof(request_bytes)) || (eventIsOpen(state) && ttl == 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    error = LUNASHELL::decodeCodexEventPayload(values[6], std::strlen(values[6]), s_event_candidate);
    if (error != ESP_OK) {
        return error;
    }
    const bool answered = state == EventState::Received || state == EventState::Delivered;
    if ((answered && (s_event_candidate.kind != LUNASHELL::CodexEventKind::Question ||
                     s_event_candidate.selected_id[0] == '\0')) ||
        (state == EventState::Pending && s_event_candidate.selected_id[0] != '\0')) {
        return ESP_ERR_INVALID_ARG;
    }
    if (std::strcmp(values[3], s_dismissed_event_id) == 0) {
        s_event_received_for_nonce = true;
        return ESP_OK;
    }
    const bool same_request = std::strcmp(values[3], s_event_snapshot.request_id) == 0;
    if (same_request && !sameEventContent(s_event_snapshot, s_event_candidate)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (!same_request && s_event_snapshot.kind == LUNASHELL::CodexEventKind::Question &&
        eventIsOpen(s_event_snapshot.state)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (same_request && !eventIsOpen(s_event_snapshot.state) &&
        s_event_snapshot.state != EventState::None && state != s_event_snapshot.state) {
        // A terminal ticket cannot be revived by an older authenticated frame.
        return ESP_ERR_INVALID_STATE;
    }
    if (same_request && s_event_snapshot.selected_id[0] != '\0' && answered &&
        std::strcmp(s_event_snapshot.selected_id, s_event_candidate.selected_id) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const bool keep_local_reply = same_request && state == EventState::Pending &&
        (s_event_snapshot.state == EventState::Sending || s_event_snapshot.state == EventState::Received);
    const bool locally_expired = same_request && eventIsOpen(s_event_snapshot.state) &&
        now >= s_event_snapshot.expires_at_us;
    bool changed = !same_request || (!keep_local_reply && state != s_event_snapshot.state) ||
        !s_event_snapshot.data_valid || s_event_snapshot.data_stale;
    if (!same_request) {
        static_cast<LUNASHELL::CodexEventPayload &>(s_event_snapshot) = s_event_candidate;
        std::memcpy(s_event_snapshot.request_id, values[3], sizeof(s_event_snapshot.request_id));
        s_event_snapshot.expires_at_us = now + static_cast<int64_t>(ttl) * 1000000LL;
        s_reply_pending = false;
    } else {
        if (answered) {
            std::memcpy(s_event_snapshot.selected_id, s_event_candidate.selected_id, sizeof(s_event_snapshot.selected_id));
        }
        const int64_t candidate_deadline = now + static_cast<int64_t>(ttl) * 1000000LL;
        if (candidate_deadline < s_event_snapshot.expires_at_us) {
            s_event_snapshot.expires_at_us = candidate_deadline;
        }
    }
    if (!keep_local_reply) {
        s_event_snapshot.state = state;
    }
    // A retransmitted snapshot cannot revive an elapsed local decision window.
    if (locally_expired || (eventIsOpen(s_event_snapshot.state) && now >= s_event_snapshot.expires_at_us)) {
        s_event_snapshot.state = EventState::Expired;
        changed = true;
    }
    s_event_snapshot.data_valid = true;
    s_event_snapshot.data_stale = false;
    s_event_snapshot.health = LUNASHELL::DeviceHealth::Ready;
    s_event_snapshot.last_error = s_event_snapshot.state == EventState::Expired ? ESP_ERR_TIMEOUT : ESP_OK;
    s_event_snapshot.last_attempt_at_us = now;
    s_event_snapshot.last_success_at_us = now;
    if (!eventIsOpen(s_event_snapshot.state) || answered) {
        s_reply_pending = false;
    }
    s_event_received_for_nonce = true;
    if (changed) {
        ++s_event_snapshot.revision;
        ESP_LOGI(kTag, "CCP EVENT authenticated: kind=%u state=%u", static_cast<unsigned>(s_event_snapshot.kind),
                 static_cast<unsigned>(s_event_snapshot.state));
    }
    error = publishEventLocked();
    if (error == ESP_OK && !same_request && state == EventState::Pending) {
        error = LUNASHELL_power_manager_note_codex_notification();
        if (error != ESP_OK) {
            s_event_snapshot.last_error = error;
            ++s_event_snapshot.revision;
            const esp_err_t publication_error = publishEventLocked();
            if (publication_error != ESP_OK) {
                ESP_LOGE(kTag, "CCP wake failure report failed (%s)", esp_err_to_name(publication_error));
            }
        }
    }
    return error;
}

// payload: Complete bounded acknowledgement for the exact confirmed response.
// source: Current paired endpoint; no unrelated sender can complete screen31.
// Exception: Returns authentication, identity, state or publication errors unchanged.
esp_err_t acceptReplyAck(const char *payload, const sockaddr_in &source)
{
    static constexpr const char *names[] = {"device", "pc", "nonce", "id", "choice", "status", "proof"};
    char *values[7]{};
    if (!parseEventFields(payload, "CCP1|REPLYACK|", names, 7, values)) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> guard(s_mutex);
    esp_err_t error = admitEventEnvelopeLocked(source, values[0], values[1], values[2], true);
    if (error != ESP_OK) {
        return error;
    }
    const int length = std::snprintf(s_event_proof_input, sizeof(s_event_proof_input),
        "reply-ack|%s|%s|%s|%s|%s|%s", values[0], values[1], values[2], values[3], values[4], values[5]);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(s_event_proof_input)) {
        return ESP_ERR_INVALID_SIZE;
    }
    error = verifyEventProofLocked(values[6]);
    if (error != ESP_OK) {
        return error;
    }
    using EventState = LUNASHELL::CodexEventState;
    if (std::strcmp(values[3], s_event_snapshot.request_id) != 0 ||
        std::strcmp(values[4], s_event_snapshot.selected_id) != 0 ||
        (s_event_snapshot.state != EventState::Sending && s_event_snapshot.state != EventState::Received &&
         s_event_snapshot.state != EventState::Delivered)) {
        return ESP_ERR_INVALID_STATE;
    }
    EventState state{};
    if (std::strcmp(values[5], "received") == 0) {
        state = EventState::Received;
    } else if (std::strcmp(values[5], "delivered") == 0) {
        state = EventState::Delivered;
    } else if (std::strcmp(values[5], "expired") == 0) {
        state = EventState::Expired;
        error = ESP_ERR_TIMEOUT;
    } else if (std::strcmp(values[5], "cancelled") == 0) {
        state = EventState::Cancelled;
        error = ESP_ERR_INVALID_STATE;
    } else if (std::strcmp(values[5], "conflict") == 0 || std::strcmp(values[5], "unknown") == 0) {
        state = EventState::Rejected;
        error = ESP_ERR_INVALID_RESPONSE;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_event_snapshot.state == EventState::Delivered && state != EventState::Delivered) {
        return ESP_ERR_INVALID_STATE;
    }
    const int64_t now = esp_timer_get_time();
    if (eventIsOpen(s_event_snapshot.state) && now >= s_event_snapshot.expires_at_us) {
        state = EventState::Expired;
        error = ESP_ERR_TIMEOUT;
    }
    if (state != s_event_snapshot.state || error != s_event_snapshot.last_error) {
        ++s_event_snapshot.revision;
    }
    s_event_snapshot.state = state;
    s_event_snapshot.last_error = error;
    s_event_snapshot.last_attempt_at_us = now;
    s_event_snapshot.last_success_at_us = s_event_snapshot.last_attempt_at_us;
    s_event_snapshot.data_valid = true;
    s_event_snapshot.data_stale = false;
    s_event_snapshot.health = LUNASHELL::DeviceHealth::Ready;
    s_reply_pending = false;
    ESP_LOGI(kTag, "CCP reply acknowledgement: state=%u", static_cast<unsigned>(state));
    const esp_err_t publication_error = publishEventLocked();
    return error != ESP_OK ? error : publication_error;
}

// No parameters: worker retries only the already confirmed ID/choice, never a different action.
// Exception: Transport errors remain canonical; an uncertain send cannot report success.
void maintainEvent()
{
    std::lock_guard<std::mutex> guard(s_mutex);
    using EventState = LUNASHELL::CodexEventState;
    const int64_t now = esp_timer_get_time();
    bool changed = false;
    if (eventIsOpen(s_event_snapshot.state) && now >= s_event_snapshot.expires_at_us) {
        s_event_snapshot.state = EventState::Expired;
        s_event_snapshot.last_error = ESP_ERR_TIMEOUT;
        s_reply_pending = false;
        changed = true;
        ESP_LOGW(kTag, "CCP request expired without an implicit response");
    }
    if (s_event_snapshot.state != EventState::None && s_event_snapshot.data_valid &&
        now - s_event_snapshot.last_success_at_us >= kReadyExpiryUs) {
        s_event_snapshot.data_valid = false;
        s_event_snapshot.data_stale = true;
        s_event_snapshot.health = LUNASHELL::DeviceHealth::Degraded;
        s_event_snapshot.last_error = ESP_ERR_TIMEOUT;
        changed = true;
    }
    if (s_reply_pending && s_pending == Command::None && s_link_snapshot.state == State::Connected &&
        s_ready_nonce[0] != '\0' && now >= s_next_reply_at_us) {
        s_next_reply_at_us = now + kFindIntervalUs;
        char input[256]{};
        const int input_size = std::snprintf(input, sizeof(input), "reply|%s|%s|%s|%s|%s",
            s_link_snapshot.device_id, s_link_snapshot.paired_pc_id, s_ready_nonce,
            s_event_snapshot.request_id, s_event_snapshot.selected_id);
        uint8_t proof[32]{};
        char proof_hex[65]{};
        esp_err_t error = input_size > 0 && static_cast<size_t>(input_size) < sizeof(input)
            ? hmacSha256(s_persisted_link.link_key, sizeof(s_persisted_link.link_key), input, proof)
            : ESP_ERR_INVALID_SIZE;
        if (error == ESP_OK) {
            error = encodeHex(proof, sizeof(proof), proof_hex, sizeof(proof_hex));
        }
        char packet[kMaximumDatagramBytes + 1]{};
        if (error == ESP_OK) {
            const int size = std::snprintf(packet, sizeof(packet),
                "CCP1|REPLY|device=%s|pc=%s|nonce=%s|id=%s|choice=%s|proof=%s",
                s_link_snapshot.device_id, s_link_snapshot.paired_pc_id, s_ready_nonce,
                s_event_snapshot.request_id, s_event_snapshot.selected_id, proof_hex);
            in_addr address{};
            if (size <= 0 || static_cast<size_t>(size) >= sizeof(packet)) {
                error = ESP_ERR_INVALID_SIZE;
            } else if (inet_pton(AF_INET, s_link_snapshot.peer_ip_address, &address) != 1) {
                error = ESP_ERR_INVALID_STATE;
            } else {
                std::memcpy(s_reply_nonce, s_ready_nonce, sizeof(s_reply_nonce));
                error = sendDatagram(packet, address, kPluginPort, "CCP REPLY");
            }
        }
        wipe(proof, sizeof(proof));
        wipe(proof_hex, sizeof(proof_hex));
        wipe(input, sizeof(input));
        wipe(packet, sizeof(packet));
        if (error != s_event_snapshot.last_error) {
            s_event_snapshot.last_error = error;
            changed = true;
        }
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "CCP reply send failed; confirmed identity retained (%s)", esp_err_to_name(error));
        }
    }
    if (changed) {
        ++s_event_snapshot.revision;
    }
    if (changed || s_event_publication_pending) {
        const esp_err_t error = publishEventLocked();
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "CCP event maintenance publication failed (%s)", esp_err_to_name(error));
        }
    }
}

// payload: Complete null-terminated datagram read by the owned socket.
// source: Exact source address and port supplied by lwIP.
void handleDatagram(const char *payload, const sockaddr_in &source)
{
    State state = State::Idle;
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        state = s_link_snapshot.state;
    }
    if (state == State::Pairing) {
        char request_id[17]{};
        if (parseDiscover(payload, request_id)) {
            const esp_err_t error = sendOffer(request_id, source);
            if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(kTag, "CCP OFFER processing failed (%s)", esp_err_to_name(error));
            }
            return;
        }
        PairRequest request{};
        if (parsePair(payload, request)) {
            const esp_err_t error = acceptPair(request, source);
            if (error != ESP_OK) {
                ESP_LOGE(kTag, "CCP PAIR processing failed (%s)", esp_err_to_name(error));
            }
        } else if (parseConfirm(payload, request)) {
            const esp_err_t error = acceptConfirm(request, source);
            if (error != ESP_OK) {
                ESP_LOGE(kTag, "CCP CONFIRM processing failed (%s)", esp_err_to_name(error));
            }
        } else if (std::strncmp(payload, "CCP1|", 5) == 0) {
            ESP_LOGE(kTag, "CCP pairing datagram rejected: malformed field contract");
        }
        return;
    }
    if (state == State::Paired || state == State::Discovering || state == State::Connected) {
        PairRequest request{};
        if (parseConfirm(payload, request)) {
            // The user may already have continued to discovery when an earlier
            // ACTIVE packet was lost. The short authenticated replay stays valid.
            const esp_err_t error = acceptConfirm(request, source);
            if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(kTag, "CCP ACTIVE replay failed (%s)", esp_err_to_name(error));
            }
            return;
        }
        if (state == State::Paired) {
            return;
        }
    }
    if (state == State::Discovering || state == State::Connected) {
        if (std::strncmp(payload, "CCP1|EVENT|", 11) == 0 ||
            std::strncmp(payload, "CCP1|REPLYACK|", 14) == 0) {
            const bool acknowledgement = std::strncmp(payload, "CCP1|REPLYACK|", 14) == 0;
            const esp_err_t error = acknowledgement ? acceptReplyAck(payload, source) : acceptEvent(payload, source);
            if (error != ESP_OK) {
                ESP_LOGE(kTag, "CCP %s rejected (%s)", acknowledgement ? "REPLYACK" : "EVENT", esp_err_to_name(error));
            }
            return;
        }
        if (std::strncmp(payload, "CCP1|ACTIVITY|", 14) == 0) {
            ActivityResponse activity{};
            const esp_err_t error = parseActivity(payload, activity)
                ? acceptActivity(activity) : ESP_ERR_INVALID_ARG;
            if (error != ESP_OK) {
                ESP_LOGE(kTag, "CCP ACTIVITY rejected (%s)", esp_err_to_name(error));
            }
            return;
        }
        ReadyResponse response{};
        if (parseReady(payload, response)) {
            const esp_err_t error = acceptReady(response, source);
            if (error != ESP_OK) {
                ESP_LOGE(kTag, "CCP READY processing failed (%s)", esp_err_to_name(error));
            }
        } else if (std::strncmp(payload, "CCP1|", 5) == 0) {
            ESP_LOGE(kTag, "CCP liveness datagram rejected: malformed field contract");
        }
    }
}

// No parameters: drains every currently queued bounded UDP datagram.
void receiveDatagrams()
{
    for (;;) {
        char *payload = s_receive_buffer;
        sockaddr_in source{};
        socklen_t source_size = sizeof(source);
        const int received = recvfrom(
            s_socket,
            payload,
            kMaximumEventDatagramBytes + 1,
            MSG_DONTWAIT,
            reinterpret_cast<sockaddr *>(&source),
            &source_size
        );
        if (received < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGE(kTag, "CCP receive failed: errno=%d", errno);
            }
            return;
        }
        if (received == 0 || received > static_cast<int>(kMaximumEventDatagramBytes)) {
            ESP_LOGE(kTag, "CCP datagram rejected: length=%d", received);
            continue;
        }
        if (std::memchr(payload, '\0', static_cast<size_t>(received)) != nullptr) {
            ESP_LOGE(kTag, "CCP datagram rejected: embedded NUL");
            continue;
        }
        payload[received] = '\0';
        if (received > static_cast<int>(kMaximumDatagramBytes) && std::strncmp(payload, "CCP1|EVENT|", 11) != 0) {
            ESP_LOGE(kTag, "CCP ordinary datagram rejected: length=%d exceeds 512", received);
            continue;
        }
        handleDatagram(payload, source);
    }
}

// No parameters: expires code windows and retries retained discovery across LAN/standby loss.
// Exception: Read and send failures stay canonical until an authenticated READY succeeds.
void maintainState()
{
    State state = State::Idle;
    int64_t deadline_at_us = 0;
    int64_t next_find_at_us = 0;
    int64_t last_ready_at_us = 0;
    int64_t activation_replay_until_us = 0;
    uint32_t attempt_id = 0;
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (s_pending != Command::None) {
            return;
        }
        state = s_link_snapshot.state;
        deadline_at_us = s_link_snapshot.deadline_at_us;
        next_find_at_us = s_next_find_at_us;
        last_ready_at_us = s_last_ready_at_us;
        activation_replay_until_us = s_activation_replay_until_us;
        attempt_id = s_link_snapshot.attempt_id;
    }
    const int64_t now_us = esp_timer_get_time();
    if (activation_replay_until_us > 0 && now_us >= activation_replay_until_us) {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (s_activation_replay_until_us > 0 &&
            now_us >= s_activation_replay_until_us) {
            clearActivationReplayLocked();
        }
    }
    if (state == State::Pairing && deadline_at_us > 0 && now_us >= deadline_at_us) {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (s_pending == Command::None && s_link_snapshot.state == State::Pairing &&
            s_link_snapshot.deadline_at_us == deadline_at_us &&
            s_link_snapshot.attempt_id == attempt_id) {
            clearPairingSecretsLocked();
            if (s_candidate_present) {
                // Retire the display window, not the durable transaction. A
                // fresh candidate-key READY can finish interrupted commissioning.
                s_last_ready_at_us = 0;
                s_next_find_at_us = 0;
                s_link_snapshot.ready_at_us = 0;
                s_link_snapshot.peer_ip_address[0] = '\0';
                wipe(s_find_nonce, sizeof(s_find_nonce));
                s_activity_received_for_nonce = false;
                publishLocked(State::Discovering, ESP_ERR_TIMEOUT);
                ESP_LOGW(kTag, "CCP code expired with a saved candidate; authenticated recovery continues");
            } else {
                publishLocked(State::TimedOut, ESP_ERR_TIMEOUT);
            }
        }
        return;
    }
    if (state != State::Discovering && state != State::Connected) {
        return;
    }
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (s_pending != Command::None ||
            (s_link_snapshot.state != State::Discovering && s_link_snapshot.state != State::Connected)) {
            return;
        }
        auto &activity = s_link_snapshot.activity;
        if (activity.data_valid && activity.last_attempt_at_us > 0 &&
            now_us - activity.last_attempt_at_us >= kReadyExpiryUs) {
            activity.data_valid = false;
            activity.data_stale = true;
            activity.last_error = ESP_ERR_TIMEOUT;
            activity.health = LUNASHELL::DeviceHealth::Degraded;
            LUNASHELL::DeviceSnapshotService::instance().reportCodexLink(s_link_snapshot);
            ESP_LOGW(kTag, "CCP activity expired: no authenticated snapshot for eight seconds");
        }
    }
    if (state == State::Connected && last_ready_at_us > 0 &&
        now_us - last_ready_at_us >= kReadyExpiryUs) {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (s_pending == Command::None && s_link_snapshot.state == State::Connected &&
            s_last_ready_at_us == last_ready_at_us) {
            s_link_snapshot.peer_ip_address[0] = '\0';
            ESP_LOGW(kTag, "CCP presence expired; retained pairing will reconnect (%s)",
                     esp_err_to_name(ESP_ERR_TIMEOUT));
            publishLocked(State::Discovering, ESP_ERR_TIMEOUT);
        }
    }
    if (next_find_at_us == 0 || now_us >= next_find_at_us) {
        {
            std::lock_guard<std::mutex> guard(s_mutex);
            if (s_pending != Command::None ||
                (s_link_snapshot.state != State::Discovering && s_link_snapshot.state != State::Connected)) {
                return;
            }
            // Schedule failed attempts too. A radio transition must not create a hot loop.
            s_next_find_at_us = now_us + kFindIntervalUs;
        }
        LUNASHELL::ConnectivityObservation observation{};
        esp_err_t error = LUNASHELL::DeviceSnapshotService::instance()
            .readConnectivityObservation(observation);
        if (error == ESP_OK && !wifiOnline(observation)) {
            error = observation.radios.wifi.last_error != ESP_OK
                ? observation.radios.wifi.last_error : ESP_ERR_INVALID_STATE;
            std::lock_guard<std::mutex> guard(s_mutex);
            if (s_pending == Command::None &&
                (s_link_snapshot.state == State::Discovering || s_link_snapshot.state == State::Connected) &&
                (s_link_snapshot.state != State::Discovering || s_link_snapshot.last_error != error)) {
                ESP_LOGW(kTag, "CCP reconnect waits for active LAN: power=%u connected=%u ip=%u (%s)",
                    static_cast<unsigned>(observation.operating_state),
                    observation.radios.wifi.connected, observation.radios.wifi.ip_acquired,
                    esp_err_to_name(error));
                s_link_snapshot.peer_ip_address[0] = '\0';
                publishLocked(State::Discovering, error);
            }
            return;
        }
        if (error == ESP_OK) {
            error = sendFind(observation.radios.wifi);
        }
        if (error != ESP_OK) {
            std::lock_guard<std::mutex> guard(s_mutex);
            // Discovery is idempotent. Preserve identity and the actual failure;
            // a successful UDP send alone cannot clear the error or prove a PC is ready.
            if (s_pending == Command::None &&
                (s_link_snapshot.state == State::Discovering || s_link_snapshot.state == State::Connected)) {
                ESP_LOGE(kTag, "CCP retained discovery attempt failed; retry in two seconds (%s)",
                         esp_err_to_name(error));
                s_link_snapshot.peer_ip_address[0] = '\0';
                publishLocked(State::Discovering, error);
            }
        }
    }
}

// No parameters: applies one admitted command; persistence runs on the internal settings owner.
// Exception: Cancellation/removal failures stay canonical and never report successful cleanup.
void processCommand()
{
    Command command = Command::None;
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        command = s_pending;
    }
    switch (command) {
    case Command::StartPairing:
        startPairing();
        break;
    case Command::StartDiscovery:
        startDiscovery();
        break;
    case Command::Cancel: {
        std::lock_guard<std::mutex> guard(s_mutex);
        s_pending = Command::None;
        esp_err_t error = s_candidate_present ? eraseLinkRecord(kPendingPairingSetting) : ESP_OK;
        if (error != ESP_OK) {
            publishLocked(State::Failed, error);
            break;
        }
        wipe(&s_candidate_link, sizeof(s_candidate_link));
        s_candidate_present = false;
        clearPairingSecretsLocked();
        clearActivationReplayLocked();
        s_link_snapshot.peer_ip_address[0] = '\0';
        s_link_snapshot.ready_at_us = 0;
        publishLocked(s_link_present ? State::Paired : State::Idle, ESP_OK);
        break;
    }
    case Command::ForgetPairing: {
        std::lock_guard<std::mutex> guard(s_mutex);
        s_pending = Command::None;
        esp_err_t error = eraseLinkRecord(kPendingPairingSetting);
        if (error == ESP_OK) {
            error = eraseLinkRecord(kPairingSetting);
        }
        s_storage_error = error;
        if (error != ESP_OK) {
            publishLocked(State::Failed, error);
            break;
        }
        wipe(&s_candidate_link, sizeof(s_candidate_link));
        wipe(&s_persisted_link, sizeof(s_persisted_link));
        s_candidate_present = false;
        s_link_present = false;
        clearPairingSecretsLocked();
        clearActivationReplayLocked();
        wipe(s_link_snapshot.paired_pc_id, sizeof(s_link_snapshot.paired_pc_id));
        wipe(s_link_snapshot.peer_ip_address, sizeof(s_link_snapshot.peer_ip_address));
        s_link_snapshot.ready_at_us = 0;
        s_link_snapshot.activity = {};
        if (s_event_snapshot.state != LUNASHELL::CodexEventState::None) {
            s_event_snapshot.state = LUNASHELL::CodexEventState::Cancelled;
            s_event_snapshot.last_error = ESP_ERR_INVALID_STATE;
            s_reply_pending = false;
            ++s_event_snapshot.revision;
            const esp_err_t publication_error = publishEventLocked();
            if (publication_error != ESP_OK) {
                ESP_LOGE(kTag, "CCP Forget event invalidation failed (%s)", esp_err_to_name(publication_error));
            }
        }
        publishLocked(State::Idle, ESP_OK);
        ESP_LOGI(kTag, "CCP Forget pairing completed; only the two CCP records were removed");
        break;
    }
    case Command::None:
        break;
    }
}

// context: Unused singleton-task context; static state is protected by s_mutex.
void codexLinkWorker(void *context)
{
    (void)context;
    for (;;) {
        processCommand();
        receiveDatagrams();
        maintainState();
        maintainEvent();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kWorkerPollMs));
    }
}

// action: Rejected semantic action included without secrets.
// attempt_id: Attempt identity supplied by the caller.
// error: Exact admission error returned to the caller.
esp_err_t reject(
    LUNASHELL_codex_link_action_t action,
    uint32_t attempt_id,
    esp_err_t error
)
{
    ESP_LOGE(kTag, "CCP action %d for attempt %lu rejected (%s)",
             static_cast<int>(action), static_cast<unsigned long>(attempt_id),
             esp_err_to_name(error));
    return error;
}

}  // namespace

// No parameters: loads exact retained state and binds the fixed CCP1 device port.
// Exception: Returns the original storage/crypto error or the logged socket/task failure.
extern "C" esp_err_t LUNASHELL_codex_link_service_init(void)
{
    std::lock_guard<std::mutex> guard(s_mutex);
    if (s_worker != nullptr) {
        return s_storage_error;
    }
    const psa_status_t crypto_status = psa_crypto_init();
    if (crypto_status != PSA_SUCCESS) {
        const esp_err_t error = logCryptoFailure("CCP crypto initialization", crypto_status);
        publishLocked(State::Failed, error);
        return error;
    }
    esp_err_t error = loadPersistedLink();
    // An optional CCP storage fault keeps a repair-only command owner alive.
    // Initialization still returns the original error and the snapshot remains failed.
    s_storage_error = error;
    s_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_socket < 0) {
        ESP_LOGE(kTag, "CCP socket creation failed: errno=%d", errno);
        publishLocked(State::Failed, ESP_FAIL);
        return ESP_FAIL;
    }
    int enabled = 1;
    if (setsockopt(s_socket, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) != 0 ||
        setsockopt(s_socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0) {
        ESP_LOGE(kTag, "CCP socket option failed: errno=%d", errno);
        close(s_socket);
        s_socket = -1;
        publishLocked(State::Failed, ESP_FAIL);
        return ESP_FAIL;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kDevicePort);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s_socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        ESP_LOGE(kTag, "CCP UDP port %u bind failed: errno=%d",
                 static_cast<unsigned>(kDevicePort), errno);
        close(s_socket);
        s_socket = -1;
        publishLocked(State::Failed, ESP_FAIL);
        return ESP_FAIL;
    }
    // A monolithic PBKDF2 call must not outrank or migrate onto the frame owner.
    // CPU0 acquisition/radio tasks retain priority over this background work.
    s_worker = xTaskCreateStaticPinnedToCore(
        codexLinkWorker,
        "LUNASHELL_ccp",
        kWorkerStackSize,
        nullptr,
        kWorkerPriority,
        s_worker_stack,
        &s_worker_control,
        LUNASHELL::task_affinity::kCcpCoreId
    );
    if (s_worker == nullptr) {
        ESP_LOGE(kTag, "CCP worker creation failed (%s)", esp_err_to_name(ESP_ERR_NO_MEM));
        close(s_socket);
        s_socket = -1;
        publishLocked(State::Failed, ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    const State restored = s_candidate_present ? State::AwaitingConfirmation :
        s_link_present ? State::Paired : State::Idle;
    publishLocked(s_storage_error == ESP_OK ? restored : State::Failed, s_storage_error);
    return s_storage_error;
}

// action: Start initial pairing, explicitly confirmed replacement, discovery or cancellation.
// expected_attempt_id: Exact canonical attempt identity observed by the caller.
// Exception: Rejects implicit replacement, stale confirmations and conflicting pending commands.
extern "C" esp_err_t LUNASHELL_codex_link_request(
    LUNASHELL_codex_link_action_t action,
    uint32_t expected_attempt_id
)
{
    Command command = Command::None;
    switch (action) {
    case LUNASHELL_CODEX_LINK_ACTION_START_PAIRING:
    case LUNASHELL_CODEX_LINK_ACTION_REPLACE_PAIRING:
        command = Command::StartPairing;
        break;
    case LUNASHELL_CODEX_LINK_ACTION_START_DISCOVERY:
        command = Command::StartDiscovery;
        break;
    case LUNASHELL_CODEX_LINK_ACTION_CANCEL:
        command = Command::Cancel;
        break;
    case LUNASHELL_CODEX_LINK_ACTION_FORGET_PAIRING:
        command = Command::ForgetPairing;
        break;
    default:
        return reject(action, expected_attempt_id, ESP_ERR_INVALID_ARG);
    }
    std::lock_guard<std::mutex> guard(s_mutex);
    if (s_worker == nullptr || expected_attempt_id != s_link_snapshot.attempt_id ||
        s_pending != Command::None) {
        return reject(action, expected_attempt_id, ESP_ERR_INVALID_STATE);
    }
    const State state = s_link_snapshot.state;
    if (s_storage_error != ESP_OK && command != Command::ForgetPairing) {
        return reject(action, expected_attempt_id, s_storage_error);
    }
    if (command == Command::StartPairing) {
        const bool replacement_confirmed = action == LUNASHELL_CODEX_LINK_ACTION_REPLACE_PAIRING;
        if ((s_link_present || s_candidate_present) != replacement_confirmed) {
            ESP_LOGE(kTag, "CCP pairing admission rejected: retained=%u replacement_confirmed=%u",
                     s_link_present, replacement_confirmed);
            return reject(action, expected_attempt_id, ESP_ERR_INVALID_STATE);
        }
        if (state == State::StartingPairing || state == State::Pairing ||
            s_link_snapshot.attempt_id == UINT32_MAX) {
            return reject(action, expected_attempt_id, ESP_ERR_INVALID_STATE);
        }
        if (eventIsOpen(s_event_snapshot.state)) {
            // Explicit replacement retires the outstanding recipient ticket.
            // Keeping the old pairing on a later Cancel does not revive it.
            s_event_snapshot.state = LUNASHELL::CodexEventState::Cancelled;
            s_event_snapshot.last_error = ESP_ERR_INVALID_STATE;
            s_reply_pending = false;
            ++s_event_snapshot.revision;
            const esp_err_t event_error = publishEventLocked();
            if (event_error != ESP_OK) {
                return reject(action, expected_attempt_id, event_error);
            }
        }
        ++s_link_snapshot.attempt_id;
        s_pending = command;
        clearPairingSecretsLocked();
        clearActivationReplayLocked();
        publishLocked(State::StartingPairing, ESP_OK);
    } else if (command == Command::StartDiscovery) {
        if ((!s_link_present && !s_candidate_present) ||
            (state != State::Paired && state != State::Discovering &&
             state != State::Connected && state != State::Failed && state != State::TimedOut &&
             state != State::AwaitingConfirmation)) {
            return reject(action, expected_attempt_id, ESP_ERR_INVALID_STATE);
        }
        s_pending = command;
        publishLocked(State::Discovering, ESP_OK);
    } else if (command == Command::ForgetPairing) {
        s_pending = command;
        publishLocked(state, s_storage_error);
    } else {
        if ((state == State::Idle || state == State::Paired) && !s_candidate_present) {
            return reject(action, expected_attempt_id, ESP_ERR_INVALID_STATE);
        }
        s_pending = command;
        publishLocked(state, ESP_OK);
    }
    xTaskNotifyGive(s_worker);
    return ESP_OK;
}

// buffer: Caller-owned destination for six display characters plus terminator.
// buffer_size: Exact writable destination capacity.
// expected_attempt_id: Active pairing attempt identity from the canonical snapshot.
// Exception: Rejects stale attempts and every state outside the active code window.
extern "C" esp_err_t LUNASHELL_codex_link_copy_pairing_code(
    char *buffer,
    size_t buffer_size,
    uint32_t expected_attempt_id
)
{
    if (buffer == nullptr || buffer_size < sizeof(s_pairing_code)) {
        return ESP_ERR_INVALID_SIZE;
    }
    std::lock_guard<std::mutex> guard(s_mutex);
    if (s_link_snapshot.state != State::Pairing ||
        expected_attempt_id != s_link_snapshot.attempt_id ||
        s_pairing_code[0] == '\0' ||
        esp_timer_get_time() >= s_link_snapshot.deadline_at_us) {
        return ESP_ERR_INVALID_STATE;
    }
    std::memcpy(buffer, s_pairing_code, sizeof(s_pairing_code));
    return ESP_OK;
}

// expected_request_id: Immutable identity displayed before explicit Send confirmation.
// choice_id: Semantic option identity, never a storage index or caption.
// Exception: Returns state, freshness, expiry, schema or publication admission failures.
extern "C" esp_err_t LUNASHELL_codex_event_reply(const char *expected_request_id, const char *choice_id)
{
    std::lock_guard<std::mutex> guard(s_mutex);
    esp_err_t error = ESP_OK;
    if (expected_request_id == nullptr || choice_id == nullptr ||
        !LUNASHELL::validCodexChoiceId(choice_id)) {
        error = ESP_ERR_INVALID_ARG;
    } else if (s_worker == nullptr || s_pending != Command::None || !s_link_present ||
        s_link_snapshot.state != State::Connected || !s_event_snapshot.data_valid ||
        s_event_snapshot.data_stale || s_event_snapshot.kind != LUNASHELL::CodexEventKind::Question ||
        s_event_snapshot.state != LUNASHELL::CodexEventState::Pending ||
        std::strcmp(expected_request_id, s_event_snapshot.request_id) != 0) {
        error = ESP_ERR_INVALID_STATE;
    } else if (esp_timer_get_time() >= s_event_snapshot.expires_at_us) {
        error = ESP_ERR_TIMEOUT;
    } else {
        bool found = false;
        for (uint8_t index = 0; index < s_event_snapshot.choice_count; ++index) {
            found = found || std::strcmp(choice_id, s_event_snapshot.choices[index].id) == 0;
        }
        if (!found) {
            error = ESP_ERR_INVALID_ARG;
        }
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "CCP decision reply admission rejected (%s)", esp_err_to_name(error));
        return error;
    }
    std::memcpy(s_event_snapshot.selected_id, choice_id, std::strlen(choice_id) + 1);
    s_event_snapshot.state = LUNASHELL::CodexEventState::Sending;
    s_event_snapshot.last_error = ESP_OK;
    ++s_event_snapshot.revision;
    error = publishEventLocked();
    if (error != ESP_OK) {
        // No packet was queued. Preserve a visible, retryable admission error.
        s_event_snapshot.state = LUNASHELL::CodexEventState::Pending;
        s_event_snapshot.selected_id[0] = '\0';
        s_event_snapshot.last_error = error;
        ++s_event_snapshot.revision;
        return error;
    }
    s_reply_pending = true;
    s_next_reply_at_us = 0;
    xTaskNotifyGive(s_worker);
    return ESP_OK;
}

// expected_request_id: Exact informational or terminal event explicitly dismissed on Cody.
// Exception: Unanswered questions, changed identity and canonical publication errors remain errors.
extern "C" esp_err_t LUNASHELL_codex_event_dismiss(const char *expected_request_id)
{
    std::lock_guard<std::mutex> guard(s_mutex);
    if (expected_request_id == nullptr || std::strcmp(expected_request_id, s_event_snapshot.request_id) != 0 ||
        s_event_snapshot.state == LUNASHELL::CodexEventState::None ||
        (s_event_snapshot.kind == LUNASHELL::CodexEventKind::Question && eventIsOpen(s_event_snapshot.state))) {
        ESP_LOGE(kTag, "CCP event dismiss rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    std::memcpy(s_dismissed_event_id, s_event_snapshot.request_id, sizeof(s_dismissed_event_id));
    static_cast<LUNASHELL::CodexEventPayload &>(s_event_snapshot) = {};
    s_event_snapshot.state = LUNASHELL::CodexEventState::None;
    s_event_snapshot.last_error = ESP_OK;
    s_event_snapshot.data_valid = true;
    s_event_snapshot.data_stale = false;
    s_reply_pending = false;
    ++s_event_snapshot.revision;
    return publishEventLocked();
}

#else

// expected_request_id: Unused request in a profile without CCP support.
// choice_id: Unused choice in a profile without CCP support.
// Exception: Always returns ESP_ERR_NOT_SUPPORTED.
extern "C" esp_err_t LUNASHELL_codex_event_reply(const char *expected_request_id, const char *choice_id)
{
    (void)expected_request_id;
    (void)choice_id;
    return ESP_ERR_NOT_SUPPORTED;
}

// expected_request_id: Unused request in a profile without CCP support.
// Exception: Always returns ESP_ERR_NOT_SUPPORTED.
extern "C" esp_err_t LUNASHELL_codex_event_dismiss(const char *expected_request_id)
{
    (void)expected_request_id;
    return ESP_ERR_NOT_SUPPORTED;
}

// No parameters: CCP1 is unavailable outside the selected Cody Wi-Fi profile.
extern "C" esp_err_t LUNASHELL_codex_link_service_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

// action: Unused action in a profile without CCP1 support.
// expected_attempt_id: Unused canonical identity in this profile.
extern "C" esp_err_t LUNASHELL_codex_link_request(
    LUNASHELL_codex_link_action_t action,
    uint32_t expected_attempt_id
)
{
    (void)action;
    (void)expected_attempt_id;
    return ESP_ERR_NOT_SUPPORTED;
}

// buffer: Unused destination in a profile without CCP1 support.
// buffer_size: Unused writable capacity.
// expected_attempt_id: Unused attempt identity.
extern "C" esp_err_t LUNASHELL_codex_link_copy_pairing_code(
    char *buffer,
    size_t buffer_size,
    uint32_t expected_attempt_id
)
{
    (void)buffer;
    (void)buffer_size;
    (void)expected_attempt_id;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
