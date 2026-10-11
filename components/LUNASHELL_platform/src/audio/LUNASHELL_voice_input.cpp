// One-way voice input: the AFE owns PCM; CCP owns encrypted Wi-Fi delivery.
#include "LUNASHELL_voice_input.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "psa/crypto.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <cstring>
#include <mutex>
#include <algorithm>

namespace {
constexpr char kTag[] = "LUNASHELL_voice_input";
constexpr size_t kSamples = 160000; // Ten seconds of unacknowledged mono PCM16.
constexpr size_t kChunk = 480;
constexpr size_t kHeader = 49;
constexpr size_t kPacket = kHeader + kChunk * 2 + 16;
std::mutex s_mutex;
int16_t *s_ring;
size_t s_read, s_write, s_count, s_inflight_samples;
LUNASHELL::VoiceInputObservation s_observation{};
uint8_t s_session[16]{};
uint8_t s_presence[16]{};
uint8_t s_packet[kPacket]{};
uint8_t s_reply[kPacket]{};
uint8_t s_plain[16]{};
uint8_t s_key[32]{};
uint32_t s_sequence, s_ack_counter;
size_t s_packet_size;
int s_socket = -1;
sockaddr_in s_peer{};
int64_t s_sent_at, s_last_speech_at, s_last_ack_at;
bool s_heard_speech, s_begun, s_final;
bool s_publication_pending = false;

// value: Host-order counter encoded in the protocol header.
// output: Four owned writable bytes.
void put32(uint32_t value, uint8_t *output)
{
    for (size_t i = 0; i < 4; ++i) output[i] = static_cast<uint8_t>(value >> (24 - i * 8));
}
// input: Four authenticated network-order bytes.
uint32_t get32(const uint8_t *input)
{
    return uint32_t(input[0]) << 24 | uint32_t(input[1]) << 16 |
           uint32_t(input[2]) << 8 | uint32_t(input[3]);
}
// phase: Confirmed capture/transport transition.
// error: Original failing operation, or ESP_OK for an actual successful transition.
// Exception: Returns a real publication failure rather than admitting invisible capture.
esp_err_t publish(LUNASHELL::VoiceInputPhase phase, esp_err_t error = ESP_OK)
{
    s_observation.phase = phase;
    s_observation.error = error;
    s_observation.changed_at_us = esp_timer_get_time();
    const esp_err_t result = LUNASHELL::DeviceSnapshotService::instance().reportVoiceInput(s_observation);
    s_publication_pending = result != ESP_OK;
    if (result != ESP_OK) ESP_LOGE(kTag, "Canonical voice input publication failed (%s)", esp_err_to_name(result));
    if (error != ESP_OK) ESP_LOGE(kTag, "Voice input operation failed (%s)", esp_err_to_name(error));
    if (result != ESP_OK) {
        s_observation.phase = LUNASHELL::VoiceInputPhase::Failed;
        s_observation.error = result;
    }
    return error != ESP_OK ? error : result;
}
// No parameters: called only while the component mutex owns the utterance state.
bool active()
{
    using Phase = LUNASHELL::VoiceInputPhase;
    return s_observation.phase == Phase::Listening || s_observation.phase == Phase::Sending ||
           s_observation.phase == Phase::Cancelling ||
           s_observation.phase == Phase::Received;
}
// link_key: Existing paired credential; never exposed to consumers or logs.
// presence_nonce: Fresh READY nonce; START must prove this exact current discovery event.
// Exception: Returns original PSA-derived error without a fabricated session key.
esp_err_t derive(const uint8_t *link_key, const char *presence_nonce)
{
    char text[80] = "cody-audio-v1|";
    constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) {
        text[14 + i * 2] = hex[s_session[i] >> 4];
        text[15 + i * 2] = hex[s_session[i] & 15];
    }
    text[46] = '|';
    std::memcpy(text + 47, presence_nonce, 32);
    for (size_t i = 0; i < 16; ++i) {
        const char pair[3] = {presence_nonce[i * 2], presence_nonce[i * 2 + 1], 0};
        s_presence[i] = static_cast<uint8_t>(std::strtoul(pair, nullptr, 16));
    }
    // The domain is 14 bytes including its trailing separator.
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    psa_status_t status = psa_import_key(&attributes, link_key, 32, &key);
    const bool imported = status == PSA_SUCCESS;
    psa_reset_key_attributes(&attributes);
    size_t length = 0;
    if (status == PSA_SUCCESS) status = psa_mac_compute(key, PSA_ALG_HMAC(PSA_ALG_SHA_256),
        reinterpret_cast<const uint8_t *>(text), 79, s_key, sizeof(s_key), &length);
    const psa_status_t cleanup = imported ? psa_destroy_key(key) : PSA_SUCCESS;
    if (status != PSA_SUCCESS || cleanup != PSA_SUCCESS || length != sizeof(s_key)) {
        ESP_LOGE(kTag, "Audio key derivation failed: PSA=%ld cleanup=%ld", long(status), long(cleanup));
        return ESP_FAIL;
    }
    return ESP_OK;
}
// encrypt: Selects independent DAT1/ACK1 nonce domains.
// packet: Immutable authenticated protocol header followed by input/ciphertext.
// input_size: Actual plaintext/ciphertext bytes including tag only when decrypting.
// output: Component-owned result buffer.
// output_capacity: Exact available bytes.
// output_size: Actual successfully produced bytes.
// Exception: Returns logged PSA crypto failure without accepting plaintext.
esp_err_t crypt(bool encrypt, const uint8_t *packet, size_t input_size,
                uint8_t *output, size_t output_capacity, size_t &output_size)
{
    uint8_t nonce[12]{};
    std::memcpy(nonce, encrypt ? "DAT1" : "ACK1", 4);
    std::memcpy(nonce + 8, packet + 42, 4);
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, encrypt ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_GCM);
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    psa_status_t status = psa_import_key(&attributes, s_key, sizeof(s_key), &key);
    const bool imported = status == PSA_SUCCESS;
    psa_reset_key_attributes(&attributes);
    if (status == PSA_SUCCESS) {
        status = encrypt ? psa_aead_encrypt(key, PSA_ALG_GCM, nonce, sizeof(nonce), packet, kHeader,
            packet + kHeader, input_size, output, output_capacity, &output_size)
            : psa_aead_decrypt(key, PSA_ALG_GCM, nonce, sizeof(nonce), packet, kHeader,
            packet + kHeader, input_size, output, output_capacity, &output_size);
    }
    const psa_status_t cleanup = imported ? psa_destroy_key(key) : PSA_SUCCESS;
    if (status != PSA_SUCCESS || cleanup != PSA_SUCCESS) {
        ESP_LOGE(kTag, "Audio %s failed: PSA=%ld cleanup=%ld", encrypt ? "encryption" : "authentication", long(status), long(cleanup));
        return ESP_FAIL;
    }
    return ESP_OK;
}
// device_id: Paired identity already validated by CCP.
// flags: Start=1, PCM=2 or end=3.
// Exception: Returns original crypto error, preserving queue contents until ACK.
esp_err_t prepare(const char *device_id, uint8_t flags)
{
    std::memcpy(s_packet, "CYA1", 4);
    for (size_t i = 0; i < 6; ++i) {
        const char pair[3] = {device_id[i * 2], device_id[i * 2 + 1], 0};
        s_packet[4 + i] = static_cast<uint8_t>(std::strtoul(pair, nullptr, 16));
    }
    std::memcpy(s_packet + 10, s_session, 16);
    std::memcpy(s_packet + 26, s_presence, 16);
    put32(s_sequence, s_packet + 42);
    s_packet[46] = flags;
    s_inflight_samples = flags == 2 ? std::min(kChunk, s_count) : 0;
    const size_t bytes = s_inflight_samples * 2;
    s_packet[47] = static_cast<uint8_t>(bytes >> 8);
    s_packet[48] = static_cast<uint8_t>(bytes);
    for (size_t i = 0; i < s_inflight_samples; ++i) {
        const uint16_t value = static_cast<uint16_t>(s_ring[(s_read + i) % kSamples]);
        s_packet[kHeader + i * 2] = static_cast<uint8_t>(value);
        s_packet[kHeader + i * 2 + 1] = static_cast<uint8_t>(value >> 8);
    }
    // PSA does not promise overlapping input/output; ciphertext has its own owned scratch.
    size_t encrypted = 0;
    const esp_err_t error = crypt(true, s_packet, bytes, s_reply, sizeof(s_reply), encrypted);
    if (error != ESP_OK) return error;
    std::memcpy(s_packet + kHeader, s_reply, encrypted);
    s_packet_size = kHeader + encrypted;
    s_sent_at = 0;
    return ESP_OK;
}
}

// trigger: Explicit event identity; both inputs enter the same capture owner.
// Exception: Returns original acquisition/admission/allocation error and logs rejection.
esp_err_t LUNASHELL::requestVoiceInput(VoiceInputTrigger trigger)
{
    std::lock_guard<std::mutex> guard(s_mutex);
    if (trigger != VoiceInputTrigger::WakeWord && trigger != VoiceInputTrigger::DoubleTap) {
        ESP_LOGE(kTag, "Voice input rejected unknown trigger (%s)", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    VoiceSnapshot voice{};
    const esp_err_t read_error = DeviceSnapshotService::instance().readVoiceObservation(voice);
    if (read_error != ESP_OK || !voice.data_valid || voice.data_stale || voice.state != VoiceState::Listening || active()) {
        const esp_err_t error = read_error != ESP_OK ? read_error : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Voice input request rejected (%s)", esp_err_to_name(error));
        return error;
    }
    s_observation = {};
    s_observation.trigger = trigger;
    s_observation.started_at_us = esp_timer_get_time();
    if (s_ring == nullptr) s_ring = static_cast<int16_t *>(heap_caps_malloc(kSamples * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_ring == nullptr) {
        publish(VoiceInputPhase::Failed, ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    s_read = s_write = s_count = s_inflight_samples = 0;
    s_sequence = s_ack_counter = 0;
    s_packet_size = 0;
    s_begun = s_final = s_heard_speech = false;
    s_last_speech_at = s_last_ack_at = s_observation.started_at_us;
    esp_fill_random(s_session, sizeof(s_session));
    const esp_err_t publication_error = publish(VoiceInputPhase::Listening);
    if (publication_error != ESP_OK) return publication_error;
    LUNASHELL_power_manager_note_activity();
    return ESP_OK;
}
// samples: Real enhanced mono PCM; never a diagnostic or synthetic stream.
// count: Successfully acquired samples, bounded by the AFE result.
// speech: Canonical inference VAD state for this block.
// error: Original acquisition failure.
void LUNASHELL::feedVoiceInput(const int16_t *samples, size_t count, bool speech, esp_err_t error)
{
    std::lock_guard<std::mutex> guard(s_mutex);
    if (s_observation.phase != VoiceInputPhase::Listening) return;
    if (error != ESP_OK || samples == nullptr || count == 0 || count > kSamples - s_count) {
        publish(VoiceInputPhase::Failed, error != ESP_OK ? error : ESP_ERR_INVALID_SIZE);
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (speech) {
        s_heard_speech = true;
        s_last_speech_at = now;
    }
    for (size_t i = 0; i < count; ++i) {
        s_ring[s_write] = samples[i];
        s_write = (s_write + 1) % kSamples;
    }
    s_count += count;
    s_observation.captured_samples += static_cast<uint32_t>(count);
    LUNASHELL_power_manager_note_activity();
    if ((s_heard_speech && now - s_last_speech_at >= 1200000) || now - s_observation.started_at_us >= 120000000) {
        publish(VoiceInputPhase::Sending);
    } else if (!s_heard_speech && now - s_observation.started_at_us >= 8000000) {
        publish(VoiceInputPhase::Failed, ESP_ERR_TIMEOUT);
    }
}
// No parameters: explicit touch cancellation never submits a partial recording.
esp_err_t LUNASHELL::cancelVoiceInput()
{
    std::lock_guard<std::mutex> guard(s_mutex);
    if (!active()) {
        ESP_LOGE(kTag, "Voice cancellation rejected (%s)", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_begun) {
        // No END can have left before START acknowledgement; the PC expires partial state.
        return publish(VoiceInputPhase::Cancelled);
    } else {
        // Retire the old ciphertext and use a new sequence/nonce for explicit CANCEL.
        ++s_sequence;
        s_packet_size = 0;
        return publish(VoiceInputPhase::Cancelling);
    }
}
// No parameters: an explicit microphone pause must not be undone by transport activity.
// Exception: Returns the original canonical publication failure.
esp_err_t LUNASHELL::pauseVoiceInput()
{
    std::lock_guard<std::mutex> guard(s_mutex);
    if (!active()) {
        return ESP_OK;
    }
    s_count = 0;
    s_packet_size = 0;
    // A transmitted END can already have reached native input; a local pause cannot revoke it.
    const bool input_may_have_left = s_observation.phase != VoiceInputPhase::Listening && s_begun;
    return publish(input_may_have_left ? VoiceInputPhase::Unconfirmed : VoiceInputPhase::Cancelled);
}

// link_key: Freshly retained paired credential, borrowed by the CCP worker.
// device_id: Stable paired device identity.
// peer_ip: Authenticated current PC endpoint.
// presence_nonce: Real current CCP discovery nonce, retained for this utterance.
// connected: Real CCP liveness, not saved pairing alone.
bool LUNASHELL::tickVoiceInputTransport(const uint8_t *link_key, const char *device_id,
                                       const char *peer_ip, const char *presence_nonce, bool connected)
{
    std::lock_guard<std::mutex> guard(s_mutex);
    if (s_publication_pending) {
        // Retry only the retained failed observation, with its original timestamp/error.
        const esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().reportVoiceInput(s_observation);
        s_publication_pending = error != ESP_OK;
        if (error != ESP_OK) ESP_LOGE(kTag, "Failed voice observation publication retry (%s)", esp_err_to_name(error));
    }
    if (!active()) return false;
    if (!connected) {
        publish(VoiceInputPhase::Failed, ESP_ERR_INVALID_STATE);
        return false;
    }
    const int64_t now = esp_timer_get_time();
    LUNASHELL_power_manager_note_activity();
    if (now - s_observation.started_at_us > 150000000) {
        publish(VoiceInputPhase::Failed, ESP_ERR_TIMEOUT);
        return false;
    }
    if (now - s_last_ack_at > 10000000) {
        publish(VoiceInputPhase::Failed, ESP_ERR_TIMEOUT);
        return false;
    }
    if (s_socket < 0) {
        s_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s_socket < 0) {
            ESP_LOGE(kTag, "Audio socket creation failed: errno=%d", errno);
            publish(VoiceInputPhase::Failed, ESP_FAIL);
            return false;
        }
        const int flags = fcntl(s_socket, F_GETFL, 0);
        if (flags < 0 || fcntl(s_socket, F_SETFL, flags | O_NONBLOCK) < 0) {
            ESP_LOGE(kTag, "Audio socket flags failed: errno=%d", errno);
            if (close(s_socket) != 0) {
                ESP_LOGE(kTag, "Audio socket cleanup failed: errno=%d", errno);
            }
            s_socket = -1;
            publish(VoiceInputPhase::Failed, ESP_FAIL);
            return false;
        }
    }
    if (!s_begun && s_packet_size == 0) {
        s_peer = {};
        s_peer.sin_family = AF_INET;
        s_peer.sin_port = htons(42119);
        if (inet_pton(AF_INET, peer_ip, &s_peer.sin_addr) != 1) {
            publish(VoiceInputPhase::Failed, ESP_ERR_INVALID_ARG);
            return false;
        }
        const esp_err_t key_error = derive(link_key, presence_nonce);
        if (key_error != ESP_OK) {
            publish(VoiceInputPhase::Failed, key_error);
            return false;
        }
        const esp_err_t error = prepare(device_id, 1);
        if (error != ESP_OK) { publish(VoiceInputPhase::Failed, error); return false; }
    }
    sockaddr_in source{};
    socklen_t source_size = sizeof(source);
    const int received = recvfrom(s_socket, s_reply, sizeof(s_reply), MSG_DONTWAIT,
        reinterpret_cast<sockaddr *>(&source), &source_size);
    if (received >= 0 && (source.sin_addr.s_addr != s_peer.sin_addr.s_addr || source.sin_port != s_peer.sin_port)) {
        ESP_LOGE(kTag, "Audio reply rejected: unexpected endpoint");
    } else if (received == int(kHeader + 5 + 16) && s_reply[46] == 0 && s_reply[47] == 0 && s_reply[48] == 5 &&
        std::memcmp(s_reply, "CYA2", 4) == 0 &&
        std::memcmp(s_reply + 4, s_packet + 4, 38) == 0 && get32(s_reply + 42) > s_ack_counter) {
        size_t decoded = 0;
        const esp_err_t error = crypt(false, s_reply, size_t(received) - kHeader, s_plain, sizeof(s_plain), decoded);
        if (error == ESP_OK && decoded == 5 && get32(s_plain) == s_sequence) {
            s_ack_counter = get32(s_reply + 42);
            s_last_ack_at = now;
            const uint8_t status = s_plain[4];
            if (status < 1 || status > 6) {
                ESP_LOGE(kTag, "Audio acknowledgement status rejected: %u", unsigned(status));
                publish(VoiceInputPhase::Failed, ESP_ERR_INVALID_RESPONSE);
                return false;
            }
            if (status == 6) {
                publish(VoiceInputPhase::Unconfirmed, ESP_ERR_INVALID_RESPONSE);
                return false;
            }
            if (status == 5) {
                publish(s_observation.phase == VoiceInputPhase::Cancelling ? VoiceInputPhase::Cancelled : VoiceInputPhase::Failed,
                    s_observation.phase == VoiceInputPhase::Cancelling ? ESP_OK : ESP_FAIL);
                return false;
            }
            if (status == 4) { publish(VoiceInputPhase::Delivered); return false; }
            if (status == 3) {
                s_final = true;
                publish(VoiceInputPhase::Received);
            } else if (!s_final && s_packet_size != 0) {
                s_read = (s_read + s_inflight_samples) % kSamples;
                s_count -= s_inflight_samples;
                s_observation.acknowledged_samples += static_cast<uint32_t>(s_inflight_samples);
                s_begun = true;
                ++s_sequence;
                s_packet_size = 0;
            }
        }
    } else if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGE(kTag, "Audio receive failed: errno=%d", errno);
        publish(VoiceInputPhase::Failed, ESP_FAIL);
        return false;
    }
    if (s_packet_size == 0 && s_begun && (s_count != 0 || s_observation.phase == VoiceInputPhase::Sending ||
        s_observation.phase == VoiceInputPhase::Cancelling)) {
        const esp_err_t error = prepare(device_id, s_observation.phase == VoiceInputPhase::Cancelling ? 4 : s_count == 0 ? 3 : 2);
        if (error != ESP_OK) { publish(VoiceInputPhase::Failed, error); return false; }
    }
    if (s_packet_size != 0 && (s_sent_at == 0 || now - s_sent_at >= 100000)) {
        const int sent = sendto(s_socket, s_packet, s_packet_size, 0,
            reinterpret_cast<const sockaddr *>(&s_peer), sizeof(s_peer));
        if (sent != int(s_packet_size)) {
            ESP_LOGE(kTag, "Audio send failed: errno=%d", errno);
            publish(VoiceInputPhase::Failed, ESP_FAIL);
            return false;
        }
        s_sent_at = now;
    }
    return active();
}
