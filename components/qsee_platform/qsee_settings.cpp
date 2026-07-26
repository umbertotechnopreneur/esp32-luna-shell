#include "qsee_settings.hpp"

#include <cstring>
#include <memory>
#include <new>
#include <utility>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

namespace qsee {
namespace {
constexpr char kNamespace[] = "qsee_user";
constexpr char kTag[] = "qsee_settings";
constexpr std::size_t kWorkerStackSize = 6 * 1024;
constexpr UBaseType_t kWorkerPriority = 5;

enum class SettingsOperation : uint8_t {
    Initialize,
    Close,
    SetU8,
    GetU8,
    SetI32,
    GetI32,
    SetU32,
    GetU32,
    SetI64,
    GetI64,
    SetU64,
    GetU64,
    SetString,
    GetStringSize,
    GetString,
    SetBlob,
    GetBlobSize,
    GetBlob,
    Clear,
};

union SettingsScalar {
    uint8_t u8;
    int32_t i32;
    uint32_t u32;
    int64_t i64;
    uint64_t u64;
};

struct SettingsRequest {
    SettingsOperation operation = SettingsOperation::Initialize;
    nvs_handle_t handle = 0;
    char key[NVS_KEY_NAME_MAX_SIZE] = {};
    SettingsScalar scalar{};
    void *data = nullptr;
    size_t size = 0;
    esp_err_t result = ESP_ERR_INVALID_STATE;
};

StaticTask_t s_worker_control{};
StackType_t s_worker_stack[kWorkerStackSize]{};
TaskHandle_t s_worker = nullptr;
StaticSemaphore_t s_completion_control{};
SemaphoreHandle_t s_completion = nullptr;
SettingsRequest s_request{};

const char *operation_name(SettingsOperation operation)
{
    switch (operation) {
    case SettingsOperation::Initialize:
        return "initialize";
    case SettingsOperation::Close:
        return "close";
    case SettingsOperation::SetU8:
        return "set_u8";
    case SettingsOperation::GetU8:
        return "get_u8";
    case SettingsOperation::SetI32:
        return "set_i32";
    case SettingsOperation::GetI32:
        return "get_i32";
    case SettingsOperation::SetU32:
        return "set_u32";
    case SettingsOperation::GetU32:
        return "get_u32";
    case SettingsOperation::SetI64:
        return "set_i64";
    case SettingsOperation::GetI64:
        return "get_i64";
    case SettingsOperation::SetU64:
        return "set_u64";
    case SettingsOperation::GetU64:
        return "get_u64";
    case SettingsOperation::SetString:
        return "set_string";
    case SettingsOperation::GetStringSize:
        return "get_string_size";
    case SettingsOperation::GetString:
        return "get_string";
    case SettingsOperation::SetBlob:
        return "set_blob";
    case SettingsOperation::GetBlobSize:
        return "get_blob_size";
    case SettingsOperation::GetBlob:
        return "get_blob";
    case SettingsOperation::Clear:
        return "clear";
    default:
        return "unknown";
    }
}

void log_operation_failure(
    SettingsOperation operation,
    const char *key,
    esp_err_t error
)
{
    ESP_LOGE(
        kTag,
        "Settings %s failed%s%s%s (%s)",
        operation_name(operation),
        key != nullptr && key[0] != '\0' ? " for key '" : "",
        key != nullptr && key[0] != '\0' ? key : "",
        key != nullptr && key[0] != '\0' ? "'" : "",
        esp_err_to_name(error)
    );
}

esp_err_t copy_key(SettingsRequest &request, const char *key)
{
    if (key == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t length = strnlen(key, NVS_KEY_NAME_MAX_SIZE);
    if (length == 0) {
        return ESP_ERR_NVS_INVALID_NAME;
    }
    if (length >= NVS_KEY_NAME_MAX_SIZE) {
        return ESP_ERR_NVS_KEY_TOO_LONG;
    }
    std::memcpy(request.key, key, length + 1);
    return ESP_OK;
}

esp_err_t prepare_request(
    SettingsOperation operation,
    nvs_handle_t handle,
    const char *key = nullptr
)
{
    s_request = {};
    s_request.operation = operation;
    s_request.handle = handle;
    if (key == nullptr) {
        return ESP_OK;
    }
    const esp_err_t error = copy_key(s_request, key);
    if (error != ESP_OK) {
        log_operation_failure(
            operation,
            error == ESP_ERR_NVS_KEY_TOO_LONG ? nullptr : key,
            error
        );
    }
    return error;
}

esp_err_t commit_if_success(esp_err_t result, nvs_handle_t handle)
{
    return result == ESP_OK ? nvs_commit(handle) : result;
}

esp_err_t execute_request(SettingsRequest &request)
{
    switch (request.operation) {
    case SettingsOperation::Initialize: {
        esp_err_t result = nvs_flash_init();
        if (result == ESP_OK) {
            result = nvs_open(
                request.key,
                NVS_READWRITE,
                &request.handle
            );
        }
        return result;
    }
    case SettingsOperation::Close:
        nvs_close(request.handle);
        return ESP_OK;
    case SettingsOperation::SetU8:
        return commit_if_success(
            nvs_set_u8(request.handle, request.key, request.scalar.u8),
            request.handle
        );
    case SettingsOperation::GetU8:
        return nvs_get_u8(
            request.handle,
            request.key,
            &request.scalar.u8
        );
    case SettingsOperation::SetI32:
        return commit_if_success(
            nvs_set_i32(request.handle, request.key, request.scalar.i32),
            request.handle
        );
    case SettingsOperation::GetI32:
        return nvs_get_i32(
            request.handle,
            request.key,
            &request.scalar.i32
        );
    case SettingsOperation::SetU32:
        return commit_if_success(
            nvs_set_u32(request.handle, request.key, request.scalar.u32),
            request.handle
        );
    case SettingsOperation::GetU32:
        return nvs_get_u32(
            request.handle,
            request.key,
            &request.scalar.u32
        );
    case SettingsOperation::SetI64:
        return commit_if_success(
            nvs_set_i64(request.handle, request.key, request.scalar.i64),
            request.handle
        );
    case SettingsOperation::GetI64:
        return nvs_get_i64(
            request.handle,
            request.key,
            &request.scalar.i64
        );
    case SettingsOperation::SetU64:
        return commit_if_success(
            nvs_set_u64(request.handle, request.key, request.scalar.u64),
            request.handle
        );
    case SettingsOperation::GetU64:
        return nvs_get_u64(
            request.handle,
            request.key,
            &request.scalar.u64
        );
    case SettingsOperation::SetString:
        return commit_if_success(
            nvs_set_str(
                request.handle,
                request.key,
                static_cast<const char *>(request.data)
            ),
            request.handle
        );
    case SettingsOperation::GetStringSize:
        request.size = 0;
        return nvs_get_str(
            request.handle,
            request.key,
            nullptr,
            &request.size
        );
    case SettingsOperation::GetString:
        return nvs_get_str(
            request.handle,
            request.key,
            static_cast<char *>(request.data),
            &request.size
        );
    case SettingsOperation::SetBlob:
        return commit_if_success(
            nvs_set_blob(
                request.handle,
                request.key,
                request.data,
                request.size
            ),
            request.handle
        );
    case SettingsOperation::GetBlobSize:
        request.size = 0;
        return nvs_get_blob(
            request.handle,
            request.key,
            nullptr,
            &request.size
        );
    case SettingsOperation::GetBlob:
        return nvs_get_blob(
            request.handle,
            request.key,
            request.data,
            &request.size
        );
    case SettingsOperation::Clear:
        return commit_if_success(
            nvs_erase_all(request.handle),
            request.handle
        );
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

void settings_worker(void *)
{
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        s_request.result = execute_request(s_request);
        if (xSemaphoreGive(s_completion) != pdTRUE) {
            ESP_LOGE(
                kTag,
                "Settings worker could not publish completion"
            );
        }
    }
}

esp_err_t ensure_worker()
{
    if (s_worker != nullptr) {
        return ESP_OK;
    }
    s_completion =
        xSemaphoreCreateBinaryStatic(&s_completion_control);
    if (s_completion == nullptr) {
        ESP_LOGE(kTag, "Settings completion semaphore creation failed");
        return ESP_ERR_NO_MEM;
    }
    s_worker = xTaskCreateStatic(
        settings_worker,
        "qsee_nvs",
        sizeof(s_worker_stack) / sizeof(s_worker_stack[0]),
        nullptr,
        kWorkerPriority,
        s_worker_stack,
        &s_worker_control
    );
    if (s_worker == nullptr) {
        ESP_LOGE(kTag, "Internal-stack settings worker creation failed");
        s_completion = nullptr;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(
        kTag,
        "Flash-safe settings worker ready: %u-byte internal stack",
        static_cast<unsigned>(sizeof(s_worker_stack))
    );
    return ESP_OK;
}

esp_err_t submit_request()
{
    if (s_worker == nullptr || s_completion == nullptr) {
        const esp_err_t error = ESP_ERR_INVALID_STATE;
        log_operation_failure(
            s_request.operation,
            s_request.key,
            error
        );
        return error;
    }
    while (xSemaphoreTake(s_completion, 0) == pdTRUE) {
    }
    if (xTaskNotifyGive(s_worker) != pdPASS) {
        const esp_err_t error = ESP_ERR_INVALID_STATE;
        log_operation_failure(
            s_request.operation,
            s_request.key,
            error
        );
        return error;
    }
    if (xSemaphoreTake(s_completion, portMAX_DELAY) != pdTRUE) {
        const esp_err_t error = ESP_ERR_INVALID_STATE;
        log_operation_failure(
            s_request.operation,
            s_request.key,
            error
        );
        return error;
    }
    if (s_request.result != ESP_OK) {
        log_operation_failure(
            s_request.operation,
            s_request.key,
            s_request.result
        );
    }
    return s_request.result;
}

struct InternalBufferDeleter {
    void operator()(uint8_t *buffer) const
    {
        heap_caps_free(buffer);
    }
};

using InternalBuffer = std::unique_ptr<uint8_t, InternalBufferDeleter>;

InternalBuffer allocate_internal(
    size_t size,
    SettingsOperation operation,
    const char *key
)
{
    if (size == 0) {
        return InternalBuffer{};
    }
    auto *buffer = static_cast<uint8_t *>(heap_caps_malloc(
        size,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    ));
    if (buffer == nullptr) {
        log_operation_failure(operation, key, ESP_ERR_NO_MEM);
    }
    return InternalBuffer{buffer};
}
}

UserSettingsService &UserSettingsService::instance()
{
    static UserSettingsService service;
    return service;
}

UserSettingsService::~UserSettingsService()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_initialized && s_worker != nullptr) {
        if (prepare_request(SettingsOperation::Close, _handle) == ESP_OK) {
            (void)submit_request();
        }
        _handle = 0;
        _initialized = false;
    }
}

esp_err_t UserSettingsService::initialize()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_initialized) {
        return ESP_OK;
    }

    esp_err_t error = ensure_worker();
    if (error != ESP_OK) {
        return error;
    }
    error = prepare_request(
        SettingsOperation::Initialize,
        0,
        kNamespace
    );
    if (error == ESP_OK) {
        error = submit_request();
    }
    if (error != ESP_OK) {
        _handle = 0;
        return error;
    }

    _handle = s_request.handle;
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
    if (error != ESP_OK) {
        log_operation_failure(SettingsOperation::SetU8, key, error);
        return error;
    }
    error = prepare_request(SettingsOperation::SetU8, _handle, key);
    if (error != ESP_OK) {
        return error;
    }
    s_request.scalar.u8 = value ? 1 : 0;
    return submit_request();
}

esp_err_t UserSettingsService::getBool(const char *key, bool *value) const
{
    if (value == nullptr) {
        log_operation_failure(
            SettingsOperation::GetU8,
            key,
            ESP_ERR_INVALID_ARG
        );
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    esp_err_t error = ensureInitialized();
    if (error != ESP_OK) {
        log_operation_failure(SettingsOperation::GetU8, key, error);
        return error;
    }
    error = prepare_request(SettingsOperation::GetU8, _handle, key);
    if (error == ESP_OK) {
        error = submit_request();
    }
    if (error != ESP_OK) {
        return error;
    }
    if (s_request.scalar.u8 > 1U) {
        log_operation_failure(
            SettingsOperation::GetU8,
            key,
            ESP_ERR_INVALID_RESPONSE
        );
        return ESP_ERR_INVALID_RESPONSE;
    }
    *value = s_request.scalar.u8 == 1U;
    return ESP_OK;
}

#define QSEE_SETTINGS_INT_METHODS(Suffix, Type, Field, SetOp, GetOp) \
esp_err_t UserSettingsService::set##Suffix(const char *key, Type value) \
{ \
    std::lock_guard<std::mutex> lock(_mutex); \
    esp_err_t error = ensureInitialized(); \
    if (error != ESP_OK) { \
        log_operation_failure(SettingsOperation::SetOp, key, error); \
        return error; \
    } \
    error = prepare_request(SettingsOperation::SetOp, _handle, key); \
    if (error != ESP_OK) return error; \
    s_request.scalar.Field = value; \
    return submit_request(); \
} \
esp_err_t UserSettingsService::get##Suffix(const char *key, Type *value) const \
{ \
    if (value == nullptr) { \
        log_operation_failure(SettingsOperation::GetOp, key, ESP_ERR_INVALID_ARG); \
        return ESP_ERR_INVALID_ARG; \
    } \
    std::lock_guard<std::mutex> lock(_mutex); \
    esp_err_t error = ensureInitialized(); \
    if (error != ESP_OK) { \
        log_operation_failure(SettingsOperation::GetOp, key, error); \
        return error; \
    } \
    error = prepare_request(SettingsOperation::GetOp, _handle, key); \
    if (error == ESP_OK) error = submit_request(); \
    if (error == ESP_OK) *value = s_request.scalar.Field; \
    return error; \
}

QSEE_SETTINGS_INT_METHODS(Int32, int32_t, i32, SetI32, GetI32)
QSEE_SETTINGS_INT_METHODS(UInt32, uint32_t, u32, SetU32, GetU32)
QSEE_SETTINGS_INT_METHODS(Int64, int64_t, i64, SetI64, GetI64)
QSEE_SETTINGS_INT_METHODS(UInt64, uint64_t, u64, SetU64, GetU64)

#undef QSEE_SETTINGS_INT_METHODS

esp_err_t UserSettingsService::setFloat(const char *key, float value)
{
    return setBlob(key, &value, sizeof(value));
}

esp_err_t UserSettingsService::getFloat(const char *key, float *value) const
{
    if (value == nullptr) {
        log_operation_failure(
            SettingsOperation::GetBlob,
            key,
            ESP_ERR_INVALID_ARG
        );
        return ESP_ERR_INVALID_ARG;
    }
    std::vector<uint8_t> data;
    esp_err_t error = getBlob(key, &data);
    if (error != ESP_OK) {
        return error;
    }
    if (data.size() != sizeof(float)) {
        log_operation_failure(
            SettingsOperation::GetBlob,
            key,
            ESP_ERR_INVALID_SIZE
        );
        return ESP_ERR_INVALID_SIZE;
    }
    std::memcpy(value, data.data(), sizeof(float));
    return ESP_OK;
}

esp_err_t UserSettingsService::setString(const char *key, const char *value)
{
    if (value == nullptr) {
        log_operation_failure(
            SettingsOperation::SetString,
            key,
            ESP_ERR_INVALID_ARG
        );
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    esp_err_t error = ensureInitialized();
    if (error != ESP_OK) {
        log_operation_failure(SettingsOperation::SetString, key, error);
        return error;
    }
    error = prepare_request(SettingsOperation::SetString, _handle, key);
    if (error != ESP_OK) {
        return error;
    }
    const size_t length = std::strlen(value) + 1;
    InternalBuffer buffer =
        allocate_internal(length, SettingsOperation::SetString, key);
    if (buffer == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    std::memcpy(buffer.get(), value, length);
    s_request.data = buffer.get();
    s_request.size = length;
    return submit_request();
}

esp_err_t UserSettingsService::getString(const char *key, std::string *value) const
{
    if (value == nullptr) {
        log_operation_failure(
            SettingsOperation::GetString,
            key,
            ESP_ERR_INVALID_ARG
        );
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    esp_err_t error = ensureInitialized();
    if (error != ESP_OK) {
        log_operation_failure(SettingsOperation::GetString, key, error);
        return error;
    }
    error = prepare_request(
        SettingsOperation::GetStringSize,
        _handle,
        key
    );
    if (error == ESP_OK) {
        error = submit_request();
    }
    if (error != ESP_OK) {
        return error;
    }
    const size_t length = s_request.size;
    if (length == 0) {
        log_operation_failure(
            SettingsOperation::GetString,
            key,
            ESP_ERR_INVALID_SIZE
        );
        return ESP_ERR_INVALID_SIZE;
    }
    InternalBuffer buffer =
        allocate_internal(length, SettingsOperation::GetString, key);
    if (buffer == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    error = prepare_request(SettingsOperation::GetString, _handle, key);
    if (error != ESP_OK) {
        return error;
    }
    s_request.data = buffer.get();
    s_request.size = length;
    error = submit_request();
    if (error != ESP_OK) {
        return error;
    }
    try {
        std::string output{reinterpret_cast<const char *>(buffer.get())};
        *value = std::move(output);
    } catch (const std::bad_alloc &) {
        log_operation_failure(
            SettingsOperation::GetString,
            key,
            ESP_ERR_NO_MEM
        );
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t UserSettingsService::setBlob(const char *key, const void *data, size_t size)
{
    if (data == nullptr && size != 0) {
        log_operation_failure(
            SettingsOperation::SetBlob,
            key,
            ESP_ERR_INVALID_ARG
        );
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    esp_err_t error = ensureInitialized();
    if (error != ESP_OK) {
        log_operation_failure(SettingsOperation::SetBlob, key, error);
        return error;
    }
    error = prepare_request(SettingsOperation::SetBlob, _handle, key);
    if (error != ESP_OK) {
        return error;
    }
    InternalBuffer buffer =
        allocate_internal(size, SettingsOperation::SetBlob, key);
    if (size != 0 && buffer == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (size != 0) {
        std::memcpy(buffer.get(), data, size);
    }
    s_request.data = buffer.get();
    s_request.size = size;
    return submit_request();
}

esp_err_t UserSettingsService::getBlob(const char *key, std::vector<uint8_t> *data) const
{
    if (data == nullptr) {
        log_operation_failure(
            SettingsOperation::GetBlob,
            key,
            ESP_ERR_INVALID_ARG
        );
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    esp_err_t error = ensureInitialized();
    if (error != ESP_OK) {
        log_operation_failure(SettingsOperation::GetBlob, key, error);
        return error;
    }
    error = prepare_request(
        SettingsOperation::GetBlobSize,
        _handle,
        key
    );
    if (error == ESP_OK) {
        error = submit_request();
    }
    if (error != ESP_OK) {
        return error;
    }
    const size_t size = s_request.size;
    InternalBuffer buffer =
        allocate_internal(size, SettingsOperation::GetBlob, key);
    if (size != 0 && buffer == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    error = prepare_request(SettingsOperation::GetBlob, _handle, key);
    if (error != ESP_OK) {
        return error;
    }
    s_request.data = buffer.get();
    s_request.size = size;
    error = submit_request();
    if (error != ESP_OK) {
        return error;
    }
    try {
        std::vector<uint8_t> output;
        if (size != 0) {
            output.assign(buffer.get(), buffer.get() + size);
        }
        *data = std::move(output);
    } catch (const std::bad_alloc &) {
        log_operation_failure(
            SettingsOperation::GetBlob,
            key,
            ESP_ERR_NO_MEM
        );
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t UserSettingsService::clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    esp_err_t error = ensureInitialized();
    if (error != ESP_OK) {
        log_operation_failure(SettingsOperation::Clear, nullptr, error);
        return error;
    }
    error = prepare_request(SettingsOperation::Clear, _handle);
    return error == ESP_OK ? submit_request() : error;
}

}  // namespace qsee
