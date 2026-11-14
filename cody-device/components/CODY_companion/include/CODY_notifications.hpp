#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace CODY {

constexpr size_t kNotificationCapacity = 32;
constexpr size_t kNotificationPageSize = 3;

enum class NotificationType : uint8_t {
    Message = 1,
    Information = 2,
    Question = 3,
    Success = 4,
    Warning = 5,
    Error = 6,
};

// This local history belongs to the notification owner, independently of any
// transport or account. RAM records remain until eviction or device restart.
struct Notification {
    uint32_t id = 0;
    int64_t received_us = 0;
    bool read = false;
    NotificationType type = NotificationType::Information;
    char source[32]{};
    char title[64]{};
    char body[193]{};
};

struct NotificationsPage {
    uint32_t revision = 0;
    uint32_t dropped = 0;
    uint16_t total = 0;
    uint16_t unread = 0;
    uint16_t offset = 0;
    uint8_t count = 0;
    Notification rows[kNotificationPageSize]{};
};

// type: Semantic category for presentation; must be a declared NotificationType.
// source: Required printable ASCII source, at most 31 characters.
// title: Required printable ASCII title, at most 63 characters.
// body: Printable ASCII detail, at most 192 characters; may be empty, never null.
// received_us: Positive monotonic arrival time supplied by the acquisition owner.
// id: Receives a nonzero owner-lifetime ID on admission, or zero on failure.
// Exception: Returns invalid input/size, exhausted IDs, or unsupported host runtime.
// Text buffers must remain stable during this call; none are retained by reference.
esp_err_t publish_notification(NotificationType type, const char *source, const char *title,
    const char *body, int64_t received_us, uint32_t &id);

// offset: Newest-first row offset, clamped to the last retained row.
// result: Receives one coherent page, total/unread counts and overflow count.
// Exception: Returns unsupported on hosts without the firmware owner.
esp_err_t read_notifications(uint16_t offset, NotificationsPage &result);

// id: Exact stable nonzero ID; row indices cannot identify a notification.
// result: Receives only that record, or an empty record on failure.
// Exception: Returns invalid input, not found after eviction, or unsupported host.
esp_err_t read_notification(uint32_t id, Notification &result);

// id: Exact retained notification to mark read without removing it.
// Exception: Returns invalid input, not found after eviction, or unsupported host.
esp_err_t mark_notification_read(uint32_t id);

} // namespace CODY
