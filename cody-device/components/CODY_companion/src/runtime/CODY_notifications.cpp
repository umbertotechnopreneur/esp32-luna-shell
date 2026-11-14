#include "CODY_notifications.hpp"

#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#endif

namespace CODY {
namespace {

// operation: Notification-owner operation that failed.
// cause: Static explanation without caller-provided or account information.
// error: Actual admission or lookup error returned unchanged.
esp_err_t notification_error(const char *operation, const char *cause, esp_err_t error)
{
#ifdef ESP_PLATFORM
    ESP_LOGW("CODY_notifications", "%s failed: %s (%s)", operation, cause, esp_err_to_name(error));
#else
    (void)operation;
    (void)cause;
#endif
    return error;
}

// value: Borrowed null-terminated caller buffer, stable for the entire call.
// capacity: Complete destination capacity including the required terminator.
// required: Reject empty strings when true.
// length: Receives the admitted length without its terminator.
// Exception: Returns invalid argument for null/empty/non-ASCII, or invalid size.
esp_err_t validate_text(const char *value, size_t capacity, bool required, size_t &length)
{
    length = 0;
    if (value == nullptr) return ESP_ERR_INVALID_ARG;
    for (size_t index = 0; index < capacity; ++index) {
        const auto byte = static_cast<unsigned char>(value[index]);
        if (byte == 0) {
            if (required && index == 0) return ESP_ERR_INVALID_ARG;
            length = index;
            return ESP_OK;
        }
        if (byte < 0x20 || byte > 0x7e) return ESP_ERR_INVALID_ARG;
    }
    return ESP_ERR_INVALID_SIZE;
}

// type: Caller-provided semantic category.
bool valid_type(NotificationType type)
{
    switch (type) {
    case NotificationType::Message:
    case NotificationType::Information:
    case NotificationType::Question:
    case NotificationType::Success:
    case NotificationType::Warning:
    case NotificationType::Error:
        return true;
    }
    return false;
}

#ifdef ESP_PLATFORM
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
Notification s_notifications[kNotificationCapacity]{};
uint16_t s_oldest = 0;
uint16_t s_count = 0;
uint16_t s_unread = 0;
uint32_t s_revision = 0;
uint32_t s_dropped = 0;
uint32_t s_next_id = 1;
static_assert(sizeof(s_notifications) <= 12U * 1024U, "Local notification history exceeds its RAM budget");

// No parameters: Advance the metadata revision while holding s_lock.
void revise_locked()
{
    if (s_revision != UINT32_MAX) ++s_revision;
}
#endif

} // namespace

// type: Valid semantic category, independent of the source transport.
// source: Required stable ASCII source label, bounded by Notification::source.
// title: Required stable ASCII title, bounded by Notification::title.
// body: Stable ASCII detail, bounded by Notification::body; empty is allowed.
// received_us: Positive monotonic acquisition time, without invented wall time.
// id: Receives the newly admitted owner-lifetime ID, or zero on failure.
// Exception: Returns the original validation, exhausted-ID or unsupported error.
esp_err_t publish_notification(NotificationType type, const char *source, const char *title,
    const char *body, int64_t received_us, uint32_t &id)
{
    id = 0;
    if (!valid_type(type))
        return notification_error("publish", "invalid notification type", ESP_ERR_INVALID_ARG);
    if (received_us <= 0)
        return notification_error("publish", "invalid acquisition time", ESP_ERR_INVALID_ARG);
    size_t source_length = 0;
    size_t title_length = 0;
    size_t body_length = 0;
    auto error = validate_text(source, sizeof(Notification::source), true, source_length);
    if (error != ESP_OK) return notification_error("publish", "invalid source label", error);
    error = validate_text(title, sizeof(Notification::title), true, title_length);
    if (error != ESP_OK) return notification_error("publish", "invalid title", error);
    error = validate_text(body, sizeof(Notification::body), false, body_length);
    if (error != ESP_OK) return notification_error("publish", "invalid body", error);
#ifdef ESP_PLATFORM
    portENTER_CRITICAL(&s_lock);
    if (s_next_id == 0) {
        portEXIT_CRITICAL(&s_lock);
        return notification_error("publish", "notification IDs exhausted", ESP_ERR_INVALID_STATE);
    }
    uint16_t slot = static_cast<uint16_t>((s_oldest + s_count) % kNotificationCapacity);
    if (s_count == kNotificationCapacity) {
        slot = s_oldest;
        if (!s_notifications[slot].read) --s_unread;
        s_oldest = static_cast<uint16_t>((s_oldest + 1U) % kNotificationCapacity);
        if (s_dropped != UINT32_MAX) ++s_dropped;
    } else {
        ++s_count;
    }
    auto &entry = s_notifications[slot];
    entry = {};
    entry.id = s_next_id;
    s_next_id = s_next_id == UINT32_MAX ? 0 : s_next_id + 1U;
    entry.received_us = received_us;
    entry.type = type;
    std::memcpy(entry.source, source, source_length + 1U);
    std::memcpy(entry.title, title, title_length + 1U);
    std::memcpy(entry.body, body, body_length + 1U);
    ++s_unread;
    revise_locked();
    id = entry.id;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// offset: Newest-first row offset, clamped against the same locked snapshot.
// result: Receives bounded rows and their coherent owner-wide counters.
// Exception: Returns unsupported outside the firmware runtime.
esp_err_t read_notifications(uint16_t offset, NotificationsPage &result)
{
    result = {};
#ifdef ESP_PLATFORM
    portENTER_CRITICAL(&s_lock);
    result.revision = s_revision;
    result.dropped = s_dropped;
    result.total = s_count;
    result.unread = s_unread;
    result.offset = s_count == 0 ? 0 : offset < s_count ? offset : static_cast<uint16_t>(s_count - 1U);
    for (size_t index = 0; index < kNotificationPageSize &&
            static_cast<size_t>(result.offset) + index < s_count; ++index) {
        const auto slot = (s_oldest + s_count - 1U - result.offset - index) % kNotificationCapacity;
        result.rows[index] = s_notifications[slot];
        ++result.count;
    }
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
#else
    (void)offset;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// id: Exact retained notification ID; zero is never assigned.
// result: Receives that record, never a replacement at an old index.
// Exception: Returns invalid input, not found after eviction, or unsupported host.
esp_err_t read_notification(uint32_t id, Notification &result)
{
    result = {};
    if (id == 0) return notification_error("read", "zero notification ID", ESP_ERR_INVALID_ARG);
#ifdef ESP_PLATFORM
    portENTER_CRITICAL(&s_lock);
    for (size_t index = 0; index < s_count; ++index) {
        const auto &entry = s_notifications[(s_oldest + index) % kNotificationCapacity];
        if (entry.id != id) continue;
        result = entry;
        portEXIT_CRITICAL(&s_lock);
        return ESP_OK;
    }
    portEXIT_CRITICAL(&s_lock);
    return notification_error("read", "notification no longer retained", ESP_ERR_NOT_FOUND);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// id: Exact retained notification whose read state changes at most once.
// Exception: Returns invalid input, not found after eviction, or unsupported host.
esp_err_t mark_notification_read(uint32_t id)
{
    if (id == 0) return notification_error("mark read", "zero notification ID", ESP_ERR_INVALID_ARG);
#ifdef ESP_PLATFORM
    portENTER_CRITICAL(&s_lock);
    for (size_t index = 0; index < s_count; ++index) {
        auto &entry = s_notifications[(s_oldest + index) % kNotificationCapacity];
        if (entry.id != id) continue;
        if (!entry.read) {
            entry.read = true;
            --s_unread;
            revise_locked();
        }
        portEXIT_CRITICAL(&s_lock);
        return ESP_OK;
    }
    portEXIT_CRITICAL(&s_lock);
    return notification_error("mark read", "notification no longer retained", ESP_ERR_NOT_FOUND);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

} // namespace CODY
