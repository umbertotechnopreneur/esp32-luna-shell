#pragma once

#include <cstdint>
#include <cstring>
#include "CODY_notifications.hpp"

namespace CODY {

enum class NotificationOrigin : uint8_t { Local = 1, Remote = 2 };

struct NotificationKey {
    NotificationOrigin origin = NotificationOrigin::Remote;
    uint32_t local_id = 0;
    char remote_id[33]{};
};

// left: Stable admitted identity, independent of its current row or read state.
// right: Identity to compare within the same notification owner.
// Exception: None; a local ID is never compared with a remote request ID.
inline bool operator==(const NotificationKey &left, const NotificationKey &right)
{
    if (left.origin != right.origin) return false;
    switch (left.origin) {
    case NotificationOrigin::Local: return left.local_id == right.local_id;
    case NotificationOrigin::Remote: return std::strcmp(left.remote_id, right.remote_id) == 0;
    }
    return false;
}

struct NotificationListRow {
    NotificationKey key{};
    NotificationType type = NotificationType::Information;
    char source[64]{};
    char title[64]{};
    bool read = false;
    bool current = false;
};

// Display projection only: remote freshness never invalidates local records.
// The display owner retains this page through frame retirement and touch release.
struct NotificationListPage {
    uint16_t total = 0;
    uint16_t unread = 0;
    uint16_t offset = 0;
    uint8_t count = 0;
    uint32_t revision = 0;
    uint32_t dropped = 0;
    NotificationListRow rows[kNotificationPageSize]{};
    bool remote_current = false;
    bool remote_retained = false;
    bool remote_pending = false;
    esp_err_t local_error = ESP_OK;
    esp_err_t remote_error = ESP_OK;
    esp_err_t action_error = ESP_OK;
};

} // namespace CODY
