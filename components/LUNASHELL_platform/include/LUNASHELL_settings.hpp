#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "esp_err.h"
#include "nvs.h"

namespace LUNASHELL {

/**
 * Thread-safe persistent store for user-owned settings.
 *
 * All settings are kept in the dedicated `LUNASHELL_user` NVS namespace. The
 * service deliberately does not erase or migrate data implicitly; callers
 * must handle explicit reset operations themselves.
 */
class UserSettingsService final {
public:
    static UserSettingsService &instance();

    esp_err_t initialize();
    bool isInitialized() const;

    esp_err_t setBool(const char *key, bool value);
    esp_err_t getBool(const char *key, bool *value) const;

    esp_err_t setInt32(const char *key, int32_t value);
    esp_err_t getInt32(const char *key, int32_t *value) const;

    esp_err_t setUInt32(const char *key, uint32_t value);
    esp_err_t getUInt32(const char *key, uint32_t *value) const;

    esp_err_t setInt64(const char *key, int64_t value);
    esp_err_t getInt64(const char *key, int64_t *value) const;

    esp_err_t setUInt64(const char *key, uint64_t value);
    esp_err_t getUInt64(const char *key, uint64_t *value) const;

    esp_err_t setFloat(const char *key, float value);
    esp_err_t getFloat(const char *key, float *value) const;

    esp_err_t setString(const char *key, const char *value);
    esp_err_t getString(const char *key, std::string *value) const;

    esp_err_t setBlob(const char *key, const void *data, size_t size);
    esp_err_t getBlob(const char *key, std::vector<uint8_t> *data) const;

    // key: One explicitly selected setting; the rest of the namespace is untouched.
    // Exception: Returns the original erase/commit error, including genuine absence.
    esp_err_t eraseKey(const char *key);

    esp_err_t clear();

private:
    UserSettingsService() = default;
    ~UserSettingsService();

    UserSettingsService(const UserSettingsService &) = delete;
    UserSettingsService &operator=(const UserSettingsService &) = delete;

    esp_err_t ensureInitialized() const;

    mutable std::mutex _mutex;
    nvs_handle_t _handle = 0;
    bool _initialized = false;
};

}  // namespace LUNASHELL
