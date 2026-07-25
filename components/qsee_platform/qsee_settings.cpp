#include "qsee_settings.hpp"

#include <cstring>

#include "esp_log.h"
#include "nvs_flash.h"

namespace qsee {
namespace {
constexpr char kNamespace[] = "qsee_user";
constexpr char kTag[] = "qsee_settings";
}

UserSettingsService &UserSettingsService::instance()
{
    static UserSettingsService service;
    return service;
}

UserSettingsService::~UserSettingsService()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_initialized) {
        nvs_close(_handle);
        _initialized = false;
    }
}

esp_err_t UserSettingsService::initialize()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_initialized) {
        return ESP_OK;
    }

    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "NVS initialization failed: %s", esp_err_to_name(error));
        return error;
    }

    error = nvs_open(kNamespace, NVS_READWRITE, &_handle);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "Opening namespace failed: %s", esp_err_to_name(error));
        _handle = 0;
        return error;
    }

    _initialized = true;
    return ESP_OK;
}

bool UserSettingsService::isInitialized() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _initialized;
}

esp_err_t UserSettingsService::ensureInitialized() const
{
    return _initialized ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t UserSettingsService::setBool(const char *key, bool value)
{
    std::lock_guard<std::mutex> lock(_mutex);
    esp_err_t error = ensureInitialized();
    if (error != ESP_OK || key == nullptr) return error != ESP_OK ? error : ESP_ERR_INVALID_ARG;
    error = nvs_set_u8(_handle, key, value ? 1 : 0);
    return error == ESP_OK ? nvs_commit(_handle) : error;
}

esp_err_t UserSettingsService::getBool(const char *key, bool default_value, bool *value) const
{
    if (value == nullptr) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(_mutex);
    esp_err_t error = ensureInitialized();
    if (error != ESP_OK || key == nullptr) return error != ESP_OK ? error : ESP_ERR_INVALID_ARG;
    uint8_t raw = default_value ? 1 : 0;
    error = nvs_get_u8(_handle, key, &raw);
    if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
    if (error == ESP_OK) *value = raw != 0;
    return error;
}

#define QSEE_SETTINGS_INT_METHODS(Suffix, Type, SetFn, GetFn) \
esp_err_t UserSettingsService::set##Suffix(const char *key, Type value) \
{ \
    std::lock_guard<std::mutex> lock(_mutex); \
    esp_err_t error = ensureInitialized(); \
    if (error != ESP_OK || key == nullptr) return error != ESP_OK ? error : ESP_ERR_INVALID_ARG; \
    error = SetFn(_handle, key, value); \
    return error == ESP_OK ? nvs_commit(_handle) : error; \
} \
esp_err_t UserSettingsService::get##Suffix(const char *key, Type default_value, Type *value) const \
{ \
    if (value == nullptr) return ESP_ERR_INVALID_ARG; \
    std::lock_guard<std::mutex> lock(_mutex); \
    esp_err_t error = ensureInitialized(); \
    if (error != ESP_OK || key == nullptr) return error != ESP_OK ? error : ESP_ERR_INVALID_ARG; \
    *value = default_value; \
    error = GetFn(_handle, key, value); \
    return error == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : error; \
}

QSEE_SETTINGS_INT_METHODS(Int32, int32_t, nvs_set_i32, nvs_get_i32)
QSEE_SETTINGS_INT_METHODS(UInt32, uint32_t, nvs_set_u32, nvs_get_u32)
QSEE_SETTINGS_INT_METHODS(Int64, int64_t, nvs_set_i64, nvs_get_i64)
QSEE_SETTINGS_INT_METHODS(UInt64, uint64_t, nvs_set_u64, nvs_get_u64)

#undef QSEE_SETTINGS_INT_METHODS

esp_err_t UserSettingsService::setFloat(const char *key, float value)
{
    return setBlob(key, &value, sizeof(value));
}

esp_err_t UserSettingsService::getFloat(const char *key, float default_value, float *value) const
{
    if (value == nullptr) return ESP_ERR_INVALID_ARG;
    *value = default_value;
    std::vector<uint8_t> data;
    esp_err_t error = getBlob(key, &data);
    if (error != ESP_OK) return error;
    if (data.size() != sizeof(float)) return ESP_ERR_INVALID_SIZE;
    std::memcpy(value, data.data(), sizeof(float));
    return ESP_OK;
}

esp_err_t UserSettingsService::setString(const char *key, const char *value)
{
    if (key == nullptr || value == nullptr) return ESP_ERR_INVALID_ARG;
    StringChangeObserver observer = nullptr;
    void *observer_context = nullptr;
    esp_err_t error = ESP_OK;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        error = ensureInitialized();
        if (error != ESP_OK) return error;
        error = nvs_set_str(_handle, key, value);
        if (error == ESP_OK) {
            error = nvs_commit(_handle);
        }
        if (error == ESP_OK) {
            observer = _string_change_observer;
            observer_context = _string_change_context;
        }
    }
    if (error == ESP_OK && observer != nullptr) {
        observer(key, value, observer_context);
    }
    return error;
}

esp_err_t UserSettingsService::getString(const char *key, const char *default_value, std::string *value) const
{
    if (key == nullptr || value == nullptr) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(_mutex);
    esp_err_t error = ensureInitialized();
    if (error != ESP_OK) return error;
    size_t length = 0;
    error = nvs_get_str(_handle, key, nullptr, &length);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        *value = default_value != nullptr ? default_value : "";
        return ESP_OK;
    }
    if (error != ESP_OK) return error;
    std::string buffer(length, '\0');
    error = nvs_get_str(_handle, key, buffer.data(), &length);
    if (error == ESP_OK) *value = buffer.c_str();
    return error;
}

esp_err_t UserSettingsService::setStringChangeObserver(
    StringChangeObserver observer,
    void *context
)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const esp_err_t error = ensureInitialized();
    if (error != ESP_OK) return error;
    _string_change_observer = observer;
    _string_change_context = context;
    return ESP_OK;
}

esp_err_t UserSettingsService::setBlob(const char *key, const void *data, size_t size)
{
    if (key == nullptr || (data == nullptr && size != 0)) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(_mutex);
    esp_err_t error = ensureInitialized();
    if (error != ESP_OK) return error;
    error = nvs_set_blob(_handle, key, data, size);
    return error == ESP_OK ? nvs_commit(_handle) : error;
}

esp_err_t UserSettingsService::getBlob(const char *key, std::vector<uint8_t> *data) const
{
    if (key == nullptr || data == nullptr) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(_mutex);
    esp_err_t error = ensureInitialized();
    if (error != ESP_OK) return error;
    size_t size = 0;
    error = nvs_get_blob(_handle, key, nullptr, &size);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        data->clear();
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (error != ESP_OK) return error;
    data->resize(size);
    return nvs_get_blob(_handle, key, data->data(), &size);
}

bool UserSettingsService::contains(const char *key) const
{
    if (key == nullptr) return false;
    std::lock_guard<std::mutex> lock(_mutex);
    if (ensureInitialized() != ESP_OK) return false;
    nvs_iterator_t iterator = nullptr;
    esp_err_t err = nvs_entry_find_in_handle(_handle, NVS_TYPE_ANY, &iterator);
    while (err == ESP_OK && iterator != nullptr) {
        nvs_entry_info_t info{};
        if (nvs_entry_info(iterator, &info) == ESP_OK && std::strcmp(info.key, key) == 0) {
            nvs_release_iterator(iterator);
            return true;
        }
        err = nvs_entry_next(&iterator);
    }
    nvs_release_iterator(iterator);
    return false;
}

esp_err_t UserSettingsService::erase(const char *key)
{
    if (key == nullptr) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(_mutex);
    esp_err_t error = ensureInitialized();
    if (error != ESP_OK) return error;
    error = nvs_erase_key(_handle, key);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    return error == ESP_OK ? nvs_commit(_handle) : error;
}

esp_err_t UserSettingsService::clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    esp_err_t error = ensureInitialized();
    if (error != ESP_OK) return error;
    error = nvs_erase_all(_handle);
    return error == ESP_OK ? nvs_commit(_handle) : error;
}

}  // namespace qsee
