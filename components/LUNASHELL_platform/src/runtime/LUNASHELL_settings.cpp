#include "LUNASHELL_settings.hpp"
#include "LUNASHELL_liveness.h"

#include <atomic>
#include <cstdlib>
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

namespace LUNASHELL {
namespace {
constexpr char kNamespace[] = "LUNASHELL_user";
constexpr char kTag[] = "LUNASHELL_settings";
constexpr std::size_t kWorkerStackSize = 6 * 1024;
constexpr UBaseType_t kWorkerPriority = 5;
constexpr uint32_t kRequestTimeoutMs = 10000U;
static_assert(
    CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000U >= kRequestTimeoutMs + 1000U,
    "Task WDT must outlive the settings deadline with recovery margin"
);

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

struct InternalBufferDeleter {
    void operator()(uint8_t *buffer) const
    {
        heap_caps_free(buffer);
    }
};

using InternalBuffer = std::unique_ptr<uint8_t, InternalBufferDeleter>;

enum class SettingsExecutorState : uint8_t {
    Uninitialized,
    Ready,
    InFlight,
    Poisoned,
};

struct SettingsRequest {
    uint32_t generation = 0;
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
InternalBuffer s_request_buffer{};
std::atomic<SettingsExecutorState> s_executor_state{
    SettingsExecutorState::Uninitialized
};
std::atomic<esp_err_t> s_poison_error{ESP_OK};
std::atomic<uint32_t> s_completed_generation{0};
uint32_t s_next_generation = 0;

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

esp_err_t executor_poison_error()
{
    const esp_err_t error = s_poison_error.load(std::memory_order_acquire);
    return error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
}

void poison_executor(
    SettingsOperation operation,
    const char *key,
    uint32_t generation,
    esp_err_t error,
    const char *reason
)
{
    esp_err_t expected = ESP_OK;
    (void)s_poison_error.compare_exchange_strong(
        expected,
        error,
        std::memory_order_acq_rel,
        std::memory_order_acquire
    );
    s_executor_state.store(
        SettingsExecutorState::Poisoned,
        std::memory_order_release
    );
    ESP_LOGE(
        kTag,
        "Settings executor poisoned: operation=%s%s%s%s "
        "generation=%lu reason=%s error=%s",
        operation_name(operation),
        key != nullptr && key[0] != '\0' ? " key='" : "",
        key != nullptr && key[0] != '\0' ? key : "",
        key != nullptr && key[0] != '\0' ? "'" : "",
        static_cast<unsigned long>(generation),
        reason,
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
    const SettingsExecutorState state =
        s_executor_state.load(std::memory_order_acquire);
    if (state != SettingsExecutorState::Ready) {
        const esp_err_t error = state == SettingsExecutorState::Poisoned
            ? executor_poison_error()
            : ESP_ERR_INVALID_STATE;
        log_operation_failure(operation, key, error);
        return error;
    }
    // The service mutex serializes callers. Releasing the previous buffer is
    // safe only in Ready: a timed-out request leaves the executor Poisoned and
    // retains its buffer for any late NVS completion.
    s_request_buffer.reset();
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
        const uint32_t generation = s_request.generation;
        const esp_err_t operation_result = execute_request(s_request);
        const esp_err_t watched_result = LUNASHELL_liveness_watch_end(
            LUNASHELL_LIVENESS_SETTINGS,
            operation_result
        );
        if (operation_result == ESP_OK && watched_result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "Settings watchdog cleanup failed: %s",
                esp_err_to_name(watched_result)
            );
            std::abort();
        }
        s_request.result = watched_result;
        s_completed_generation.store(
            generation,
            std::memory_order_release
        );
        if (xSemaphoreGive(s_completion) != pdTRUE) {
            poison_executor(
                s_request.operation,
                s_request.key,
                generation,
                ESP_ERR_INVALID_STATE,
                "worker could not publish completion"
            );
            std::abort();
        }
    }
}

esp_err_t ensure_worker()
{
    if (s_worker != nullptr) {
        const SettingsExecutorState state =
            s_executor_state.load(std::memory_order_acquire);
        if (state == SettingsExecutorState::Poisoned) {
            const esp_err_t error = executor_poison_error();
            log_operation_failure(
                SettingsOperation::Initialize,
                kNamespace,
                error
            );
            return error;
        }
        if (state != SettingsExecutorState::Ready) {
            log_operation_failure(
                SettingsOperation::Initialize,
                kNamespace,
                ESP_ERR_INVALID_STATE
            );
            return ESP_ERR_INVALID_STATE;
        }
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
        "LUNASHELL_nvs",
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
    s_executor_state.store(
        SettingsExecutorState::Ready,
        std::memory_order_release
    );
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
    const SettingsExecutorState state =
        s_executor_state.load(std::memory_order_acquire);
    if (state != SettingsExecutorState::Ready) {
        const esp_err_t error = state == SettingsExecutorState::Poisoned
            ? executor_poison_error()
            : ESP_ERR_INVALID_STATE;
        log_operation_failure(
            s_request.operation,
            s_request.key,
            error
        );
        return error;
    }
    if (xSemaphoreTake(s_completion, 0) == pdTRUE) {
        const esp_err_t error = ESP_ERR_INVALID_STATE;
        poison_executor(
            s_request.operation,
            s_request.key,
            s_request.generation,
            error,
            "unexpected stale completion"
        );
        return error;
    }

    const esp_err_t watch_error = LUNASHELL_liveness_watch_begin(
        LUNASHELL_LIVENESS_SETTINGS
    );
    if (watch_error != ESP_OK) {
        log_operation_failure(
            s_request.operation,
            s_request.key,
            watch_error
        );
        return watch_error;
    }
    const auto finish_watched_request = [](esp_err_t result) {
        return LUNASHELL_liveness_watch_end(
            LUNASHELL_LIVENESS_SETTINGS,
            result
        );
    };

    ++s_next_generation;
    if (s_next_generation == 0) {
        ++s_next_generation;
    }
    const uint32_t expected_generation = s_next_generation;
    s_request.generation = expected_generation;
    s_completed_generation.store(0, std::memory_order_release);
    s_executor_state.store(
        SettingsExecutorState::InFlight,
        std::memory_order_release
    );
    if (xTaskNotifyGive(s_worker) != pdPASS) {
        const esp_err_t error = ESP_ERR_INVALID_STATE;
        s_executor_state.store(
            SettingsExecutorState::Ready,
            std::memory_order_release
        );
        log_operation_failure(
            s_request.operation,
            s_request.key,
            error
        );
        return finish_watched_request(error);
    }
    if (xSemaphoreTake(
            s_completion,
            pdMS_TO_TICKS(kRequestTimeoutMs)
        ) != pdTRUE) {
        const esp_err_t error = ESP_ERR_TIMEOUT;
        poison_executor(
            s_request.operation,
            s_request.key,
            expected_generation,
            error,
            "completion deadline expired"
        );
        // The worker owns watch closure after a successful notification. If
        // NVS eventually returns it closes the watch before publishing its
        // late completion; if it never returns the Task WDT resets the boot.
        return error;
    }
    const uint32_t completed_generation =
        s_completed_generation.load(std::memory_order_acquire);
    if (completed_generation != expected_generation) {
        const esp_err_t error = ESP_ERR_INVALID_RESPONSE;
        poison_executor(
            s_request.operation,
            s_request.key,
            expected_generation,
            error,
            "completion generation mismatch"
        );
        return error;
    }
    s_executor_state.store(
        SettingsExecutorState::Ready,
        std::memory_order_release
    );
    if (s_request.result != ESP_OK) {
        log_operation_failure(
            s_request.operation,
            s_request.key,
            s_request.result
        );
    }
    return s_request.result;
}

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

class RequestBufferReleaseGuard final {
public:
    ~RequestBufferReleaseGuard()
    {
        if (s_executor_state.load(std::memory_order_acquire) ==
            SettingsExecutorState::Ready) {
            s_request.data = nullptr;
            s_request.size = 0;
            s_request_buffer.reset();
        }
    }
};
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
        const SettingsExecutorState state =
            s_executor_state.load(std::memory_order_acquire);
        if (state == SettingsExecutorState::Ready) {
            return ESP_OK;
        }
        const esp_err_t error = state == SettingsExecutorState::Poisoned
            ? executor_poison_error()
            : ESP_ERR_INVALID_STATE;
        log_operation_failure(
            SettingsOperation::Initialize,
            kNamespace,
            error
        );
        return error;
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
    return _initialized &&
        s_executor_state.load(std::memory_order_acquire) ==
            SettingsExecutorState::Ready;
}

esp_err_t UserSettingsService::ensureInitialized() const
{
    if (s_executor_state.load(std::memory_order_acquire) ==
        SettingsExecutorState::Poisoned) {
        return executor_poison_error();
    }
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

#define LUNASHELL_SETTINGS_INT_METHODS(Suffix, Type, Field, SetOp, GetOp) \
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

LUNASHELL_SETTINGS_INT_METHODS(Int32, int32_t, i32, SetI32, GetI32)
LUNASHELL_SETTINGS_INT_METHODS(UInt32, uint32_t, u32, SetU32, GetU32)
LUNASHELL_SETTINGS_INT_METHODS(Int64, int64_t, i64, SetI64, GetI64)
LUNASHELL_SETTINGS_INT_METHODS(UInt64, uint64_t, u64, SetU64, GetU64)

#undef LUNASHELL_SETTINGS_INT_METHODS

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
    s_request_buffer =
        allocate_internal(length, SettingsOperation::SetString, key);
    if (s_request_buffer == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    RequestBufferReleaseGuard release_buffer;
    std::memcpy(s_request_buffer.get(), value, length);
    s_request.data = s_request_buffer.get();
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
    error = prepare_request(SettingsOperation::GetString, _handle, key);
    if (error != ESP_OK) {
        return error;
    }
    s_request_buffer =
        allocate_internal(length, SettingsOperation::GetString, key);
    if (s_request_buffer == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    RequestBufferReleaseGuard release_buffer;
    s_request.data = s_request_buffer.get();
    s_request.size = length;
    error = submit_request();
    if (error != ESP_OK) {
        return error;
    }
    try {
        std::string output{
            reinterpret_cast<const char *>(s_request_buffer.get())
        };
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
    s_request_buffer =
        allocate_internal(size, SettingsOperation::SetBlob, key);
    if (size != 0 && s_request_buffer == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    RequestBufferReleaseGuard release_buffer;
    if (size != 0) {
        std::memcpy(s_request_buffer.get(), data, size);
    }
    s_request.data = s_request_buffer.get();
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
    error = prepare_request(SettingsOperation::GetBlob, _handle, key);
    if (error != ESP_OK) {
        return error;
    }
    s_request_buffer =
        allocate_internal(size, SettingsOperation::GetBlob, key);
    if (size != 0 && s_request_buffer == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    RequestBufferReleaseGuard release_buffer;
    s_request.data = s_request_buffer.get();
    s_request.size = size;
    error = submit_request();
    if (error != ESP_OK) {
        return error;
    }
    try {
        std::vector<uint8_t> output;
        if (size != 0) {
            output.assign(
                s_request_buffer.get(),
                s_request_buffer.get() + size
            );
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

}  // namespace LUNASHELL
