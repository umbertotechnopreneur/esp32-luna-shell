#pragma once
#include <cstddef>
#include "esp_err.h"
namespace LUNASHELL {
class UserSettingsService {
public:
    // No parameters: fixture-owned settings transport.
    static UserSettingsService &instance();
    // No parameters: returns the configured initialization outcome.
    esp_err_t initialize();
    // key: Recorder key.
    // data: Fixed destination.
    // size: Exact admitted stored size.
    esp_err_t getBlobExact(const char *key, void *data, size_t size) const;
    // key: Recorder key.
    // data: Complete transaction bytes.
    // size: Transaction length.
    esp_err_t setBlob(const char *key, const void *data, size_t size);
};
}
