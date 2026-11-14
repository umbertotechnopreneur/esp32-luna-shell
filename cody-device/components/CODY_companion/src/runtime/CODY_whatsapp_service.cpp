#include "CODY_whatsapp_service.hpp"
#include "CODY_notifications.hpp"

#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#endif

#if defined(CODY_WHATSAPP_EXPERIMENT) && CODY_WHATSAPP_EXPERIMENT
#include "cody_whatsapp.h"
#endif

namespace CODY {
#if defined(CODY_WHATSAPP_EXPERIMENT) && CODY_WHATSAPP_EXPERIMENT
namespace {
constexpr uint32_t kStackBytes = 256U * 1024U;
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
WhatsAppObservation s_observation{WhatsAppState::Idle};
bool s_running = false;
bool s_resume_in_progress = false;
bool s_pair_after_resume = false;
bool s_pair_handoff = false;
int64_t s_message_received_us = 0;

// No parameters: Remove the transient pairing secret from the owner buffer.
void clear_qr_locked()
{
    volatile char *bytes = s_observation.qr;
    for (size_t index = 0; index < sizeof(s_observation.qr); ++index) bytes[index] = 0;
    s_observation.qr_expires_us = 0;
}

// kind: Event from the protocol owner, never a UI or hardware observation.
// payload: Borrowed QR bytes, valid only during this callback.
// size: Exact QR payload length, or zero for events without a payload.
// valid_seconds: Server-supplied QR lifetime.
// context: Reserved owner identity; currently zero.
void protocol_notice(uint32_t kind, const uint8_t *payload, size_t size, uint64_t valid_seconds, uintptr_t context)
{
    (void)context;
    const int64_t now = esp_timer_get_time();
    bool retain_message = false;
    portENTER_CRITICAL(&s_lock);
    if (!s_running || s_observation.state == WhatsAppState::Stopping) {
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    switch (kind) {
    case CODY_WHATSAPP_PAIRING_QR:
        clear_qr_locked();
        if (payload == nullptr || size == 0 || size > kWhatsAppQrCapacity ||
            valid_seconds == 0 || valid_seconds > 300 || std::memchr(payload, 0, size) != nullptr) {
            s_observation.state = WhatsAppState::Failed;
            s_observation.error = ESP_ERR_INVALID_RESPONSE;
        } else {
            std::memcpy(s_observation.qr, payload, size);
            s_observation.qr[size] = '\0';
            s_observation.qr_expires_us = now + static_cast<int64_t>(valid_seconds) * 1000000LL;
            s_observation.state = WhatsAppState::Pairing;
            s_observation.error = ESP_OK;
        }
        break;
    case CODY_WHATSAPP_PAIRING_EXPIRED:
        clear_qr_locked();
        s_observation.state = WhatsAppState::Connecting;
        break;
    case CODY_WHATSAPP_CONNECTED:
        clear_qr_locked();
        s_observation.state = WhatsAppState::Connected;
        s_observation.error = ESP_OK;
        break;
    case CODY_WHATSAPP_DISCONNECTED:
        clear_qr_locked();
        s_observation.state = WhatsAppState::Connecting;
        break;
    case CODY_WHATSAPP_LOGGED_OUT:
        clear_qr_locked();
        s_observation.state = WhatsAppState::Failed;
        s_observation.error = ESP_ERR_INVALID_STATE;
        break;
    case CODY_WHATSAPP_INCOMING:
        retain_message = true;
        if (s_observation.message_sequence != UINT32_MAX) {
            ++s_observation.message_sequence;
            s_message_received_us = now;
        }
        break;
    case CODY_WHATSAPP_UNDECRYPTABLE:
        s_observation.error = ESP_ERR_INVALID_RESPONSE;
        break;
    default:
        s_observation.error = ESP_ERR_INVALID_ARG;
        break;
    }
    if (s_observation.revision != UINT32_MAX) ++s_observation.revision;
    portEXIT_CRITICAL(&s_lock);
    if (retain_message) {
        // Notification and protocol owners must never hold each other's locks.
        uint32_t notification_id = 0;
        const auto error = publish_notification(NotificationType::Message, "WhatsApp", "New message",
            "Sender and content unavailable.", now, notification_id);
        if (error != ESP_OK) {
            ESP_LOGW("CODY_whatsapp", "Notification retention failed (%s)", esp_err_to_name(error));
            portENTER_CRITICAL(&s_lock);
            s_observation.error = error;
            if (s_observation.revision != UINT32_MAX) ++s_observation.revision;
            portEXIT_CRITICAL(&s_lock);
        }
    }
}

// argument: Non-null when this admission only resumes an existing session.
void protocol_worker(void *argument)
{
    const bool resume_only = argument != nullptr;
    int32_t result = cody_whatsapp_run(protocol_notice, 0, resume_only);
    portENTER_CRITICAL(&s_lock);
    const bool pair_now = resume_only && result == 1 && s_pair_after_resume &&
        s_observation.state != WhatsAppState::Stopping;
    s_resume_in_progress = false;
    s_pair_after_resume = false;
    s_pair_handoff = pair_now;
    if (!pair_now) {
        const bool stopping = s_observation.state == WhatsAppState::Stopping;
        clear_qr_locked();
        s_running = false;
        s_observation.running = false;
        const bool idle = (result == 0 && stopping) || result == 1;
        s_observation.state = idle ? WhatsAppState::Idle : WhatsAppState::Failed;
        s_observation.error = idle ? ESP_OK : ESP_FAIL;
        if (s_observation.revision != UINT32_MAX) ++s_observation.revision;
    }
    portEXIT_CRITICAL(&s_lock);
    if (pair_now) {
        if (cody_whatsapp_prepare() == 0) {
            portENTER_CRITICAL(&s_lock);
            const bool stop_pending = s_observation.state == WhatsAppState::Stopping;
            s_pair_handoff = false;
            portEXIT_CRITICAL(&s_lock);
            if (stop_pending) (void)cody_whatsapp_stop();
            result = cody_whatsapp_run(protocol_notice, 0, false);
        } else {
            portENTER_CRITICAL(&s_lock);
            s_pair_handoff = false;
            portEXIT_CRITICAL(&s_lock);
            result = -1;
        }
        portENTER_CRITICAL(&s_lock);
        const bool stopping = s_observation.state == WhatsAppState::Stopping;
        clear_qr_locked();
        s_running = false;
        s_observation.running = false;
        s_observation.state = result == 0 && stopping ? WhatsAppState::Idle : WhatsAppState::Failed;
        s_observation.error = result == 0 && stopping ? ESP_OK : ESP_FAIL;
        if (s_observation.revision != UINT32_MAX) ++s_observation.revision;
        portEXIT_CRITICAL(&s_lock);
    }
    vTaskDeleteWithCaps(nullptr);
}
} // namespace
#endif

// resume_only: Probe the stored identity without requesting a new QR.
// Exception: Returns the original task allocation or session reservation error.
esp_err_t admit_whatsapp(bool resume_only)
{
#if defined(CODY_WHATSAPP_EXPERIMENT) && CODY_WHATSAPP_EXPERIMENT
    portENTER_CRITICAL(&s_lock);
    if (s_running) {
        if (!resume_only && s_resume_in_progress) s_pair_after_resume = true;
        portEXIT_CRITICAL(&s_lock);
        return ESP_OK;
    }
    portEXIT_CRITICAL(&s_lock);
    if (cody_whatsapp_prepare() != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_lock);
    s_running = true;
    s_resume_in_progress = resume_only;
    s_pair_after_resume = false;
    s_observation.running = true;
    clear_qr_locked();
    s_observation.state = WhatsAppState::Connecting;
    s_observation.error = ESP_OK;
    if (s_observation.revision != UINT32_MAX) ++s_observation.revision;
    portEXIT_CRITICAL(&s_lock);
    if (xTaskCreateWithCaps(protocol_worker, "CODY_whatsapp", kStackBytes,
            resume_only ? reinterpret_cast<void *>(1) : nullptr, 5, nullptr,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        cody_whatsapp_release_unstarted();
        portENTER_CRITICAL(&s_lock);
        s_running = false;
        s_resume_in_progress = false;
        s_pair_after_resume = false;
        s_observation.running = false;
        s_observation.state = WhatsAppState::Failed;
        s_observation.error = ESP_ERR_NO_MEM;
        if (s_observation.revision != UINT32_MAX) ++s_observation.revision;
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
#else
    (void)resume_only;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t start_whatsapp() { return admit_whatsapp(false); }
esp_err_t resume_whatsapp() { return admit_whatsapp(true); }

// No parameters: An accepted stop remains pending until the worker exits.
// Exception: Returns unavailability or a rejected Rust-side stop request.
esp_err_t stop_whatsapp()
{
#if defined(CODY_WHATSAPP_EXPERIMENT) && CODY_WHATSAPP_EXPERIMENT
    portENTER_CRITICAL(&s_lock);
    if (!s_running) { portEXIT_CRITICAL(&s_lock); return ESP_ERR_INVALID_STATE; }
    s_observation.state = WhatsAppState::Stopping;
    const bool handoff = s_pair_handoff;
    clear_qr_locked();
    if (s_observation.revision != UINT32_MAX) ++s_observation.revision;
    portEXIT_CRITICAL(&s_lock);
    if (handoff) return ESP_OK;
    if (cody_whatsapp_stop() == 0) return ESP_OK;
    portENTER_CRITICAL(&s_lock);
    s_observation.state = WhatsAppState::Failed;
    s_observation.error = ESP_FAIL;
    if (s_observation.revision != UINT32_MAX) ++s_observation.revision;
    portEXIT_CRITICAL(&s_lock);
    return ESP_FAIL;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// result: Receives an independent snapshot whose QR must be wiped by the caller.
// Exception: Returns the last actual service error, if any.
esp_err_t read_whatsapp(WhatsAppObservation &result)
{
#if defined(CODY_WHATSAPP_EXPERIMENT) && CODY_WHATSAPP_EXPERIMENT
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    if (s_observation.qr_expires_us != 0 && now >= s_observation.qr_expires_us) {
        clear_qr_locked();
        if (s_observation.state == WhatsAppState::Pairing) s_observation.state = WhatsAppState::Connecting;
        if (s_observation.revision != UINT32_MAX) ++s_observation.revision;
    }
    result = s_observation;
    portEXIT_CRITICAL(&s_lock);
    return result.error;
#else
    result = {};
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// sequence: Receives the latest admitted message sequence.
// received_us: Receives its monotonic arrival time; zero means no message.
// Exception: Returns unavailable when this experimental owner is absent.
esp_err_t read_whatsapp_message(uint32_t &sequence, int64_t &received_us)
{
#if defined(CODY_WHATSAPP_EXPERIMENT) && CODY_WHATSAPP_EXPERIMENT
    portENTER_CRITICAL(&s_lock);
    sequence = s_observation.message_sequence;
    received_us = s_message_received_us;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
#else
    sequence = 0;
    received_us = 0;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

} // namespace CODY
