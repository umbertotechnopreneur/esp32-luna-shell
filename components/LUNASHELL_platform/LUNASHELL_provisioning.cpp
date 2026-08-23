#include "LUNASHELL_provisioning.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_liveness.h"
#include "LUNASHELL_platform.h"
#include "LUNASHELL_radio_memory.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"

namespace {
constexpr char kTag[] = "LUNASHELL_prov";
constexpr uint32_t kManagerOperationTimeoutMs = 10000;
constexpr uint32_t kManagerOperationTaskStackSize = 4096;
constexpr UBaseType_t kManagerOperationTaskPriority = 3;
static_assert(
    CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000U >=
        kManagerOperationTimeoutMs + 1000U,
    "Task WDT must outlive the provisioning deadline with recovery margin"
);

enum class ManagerOperation : uint8_t {
    None,
    Stop,
    Deinitialize,
};

enum {
    MANAGER_CLEANUP_UNCLAIMED = 0,
    MANAGER_CLEANUP_WAITER,
    MANAGER_CLEANUP_WORKER,
};

StaticSemaphore_t s_manager_stop_done_storage;
SemaphoreHandle_t s_manager_stop_done = nullptr;
std::atomic<ManagerOperation> s_manager_operation{ManagerOperation::None};
std::atomic<esp_err_t> s_manager_operation_result{ESP_ERR_INVALID_STATE};
std::atomic_int s_manager_cleanup_owner{MANAGER_CLEANUP_UNCLAIMED};

void finish_manager_operation_task()
{
    int expected = MANAGER_CLEANUP_UNCLAIMED;
    const bool worker_owns_cleanup =
        s_manager_cleanup_owner.compare_exchange_strong(
            expected,
            MANAGER_CLEANUP_WORKER,
            std::memory_order_acq_rel,
            std::memory_order_acquire
        ) || expected == MANAGER_CLEANUP_WORKER;
    if (worker_owns_cleanup) {
        vTaskDeleteWithCaps(nullptr);
        ESP_LOGE(kTag, "manager lifecycle worker returned after self-delete");
        std::abort();
    }

    vTaskSuspend(nullptr);
    ESP_LOGE(kTag, "manager lifecycle worker resumed after completion");
    std::abort();
}

void manager_operation_task(void *)
{
    const ManagerOperation operation =
        s_manager_operation.load(std::memory_order_acquire);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    switch (operation) {
    case ManagerOperation::Stop:
        network_prov_mgr_stop_provisioning();
        network_prov_mgr_wait();
        result = ESP_OK;
        break;
    case ManagerOperation::Deinitialize:
        result = network_prov_mgr_deinit();
        break;
    case ManagerOperation::None:
        ESP_LOGE(kTag, "manager lifecycle worker received no operation");
        break;
    }
    const esp_err_t watched_result = LUNASHELL_liveness_watch_end(
        LUNASHELL_LIVENESS_PROVISIONING,
        result
    );
    if (result == ESP_OK && watched_result != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Manager watchdog cleanup failed: %s",
            esp_err_to_name(watched_result)
        );
        std::abort();
    }
    s_manager_operation_result.store(
        watched_result,
        std::memory_order_release
    );

    if (xSemaphoreGive(s_manager_stop_done) != pdTRUE) {
        ESP_LOGE(kTag, "manager lifecycle completion signal failed");
        std::abort();
    }
    finish_manager_operation_task();
}

esp_err_t run_manager_operation_with_deadline(
    ManagerOperation requested_operation,
    const char *operation
)
{
    if (requested_operation == ManagerOperation::None || operation == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_manager_stop_done == nullptr) {
        s_manager_stop_done =
            xSemaphoreCreateBinaryStatic(&s_manager_stop_done_storage);
        if (s_manager_stop_done == nullptr) {
            ESP_LOGE(
                kTag,
                "%s: manager lifecycle completion signal creation failed",
                operation
            );
            return ESP_ERR_NO_MEM;
        }
    }
    if (xSemaphoreTake(s_manager_stop_done, 0) == pdTRUE) {
        ESP_LOGE(
            kTag,
            "%s: stale manager lifecycle completion detected",
            operation
        );
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t watch_error = LUNASHELL_liveness_watch_begin(
        LUNASHELL_LIVENESS_PROVISIONING
    );
    if (watch_error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "%s: provisioning liveness watch failed: %s",
            operation,
            esp_err_to_name(watch_error)
        );
        return watch_error;
    }

    s_manager_operation.store(requested_operation, std::memory_order_release);
    s_manager_operation_result.store(
        ESP_ERR_INVALID_STATE,
        std::memory_order_release
    );
    s_manager_cleanup_owner.store(
        MANAGER_CLEANUP_UNCLAIMED,
        std::memory_order_release
    );

    TaskHandle_t task = nullptr;
    if (xTaskCreateWithCaps(
            manager_operation_task,
            "LUNASHELL_prov_mgr",
            kManagerOperationTaskStackSize,
            nullptr,
            kManagerOperationTaskPriority,
            &task,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
        ) != pdPASS) {
        s_manager_operation.store(ManagerOperation::None);
        ESP_LOGE(kTag, "%s: manager lifecycle worker creation failed", operation);
        return LUNASHELL_liveness_watch_end(
            LUNASHELL_LIVENESS_PROVISIONING,
            ESP_ERR_NO_MEM
        );
    }

    if (xSemaphoreTake(
            s_manager_stop_done,
            pdMS_TO_TICKS(kManagerOperationTimeoutMs)
        ) != pdTRUE) {
        // The manager may still own BLE resources and the worker may still be
        // inside its stop path. It is unsafe to delete that task or reuse the
        // manager during this boot, so the caller must poison the lifecycle.
        ESP_LOGE(
            kTag,
            "%s: manager lifecycle exceeded %lu ms",
            operation,
            static_cast<unsigned long>(kManagerOperationTimeoutMs)
        );
        // Delegate task cleanup. The worker owns the still-armed watch for
        // every submitted operation, so a late completion closes it and a
        // genuinely stuck lifecycle path reaches the Task-WDT deadline.
        int cleanup_expected = MANAGER_CLEANUP_UNCLAIMED;
        (void)s_manager_cleanup_owner.compare_exchange_strong(
            cleanup_expected,
            MANAGER_CLEANUP_WORKER,
            std::memory_order_acq_rel,
            std::memory_order_acquire
        );
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t operation_result =
        s_manager_operation_result.load(std::memory_order_acquire);
    int cleanup_expected = MANAGER_CLEANUP_UNCLAIMED;
    const bool waiter_owns_cleanup =
        s_manager_cleanup_owner.compare_exchange_strong(
            cleanup_expected,
            MANAGER_CLEANUP_WAITER,
            std::memory_order_acq_rel,
            std::memory_order_acquire
        );
    if (waiter_owns_cleanup) {
        vTaskDeleteWithCaps(task);
    }
    s_manager_operation.store(ManagerOperation::None, std::memory_order_release);
    const esp_err_t result = operation_result;
    if (result == ESP_OK) {
        ESP_LOGI(kTag, "%s: manager lifecycle completed", operation);
    } else {
        ESP_LOGE(
            kTag,
            "%s: manager lifecycle failed: %s",
            operation,
            esp_err_to_name(result)
        );
    }
    return result;
}

void publish_manager_stop_failure(
    const char *operation,
    esp_err_t error,
    const char *service_name,
    bool poisoned
)
{
    LUNASHELL::DeviceSnapshotService::instance().reportBle(
        false,
        error,
        false,
        false,
        0xFFFF,
        0,
        0,
        service_name,
        true
    );
    ESP_LOGE(
        kTag,
        "%s failed: %s; provisioning lifecycle %s",
        operation,
        esp_err_to_name(error),
        poisoned ? "is poisoned for this boot" : "remains retryable"
    );
}

LUNASHELL_provisioning_state_t to_c_state(LUNASHELL::ProvisioningState state)
{
    switch (state) {
    case LUNASHELL::ProvisioningState::Idle:
        return LUNASHELL_PROVISIONING_STATE_IDLE;
    case LUNASHELL::ProvisioningState::Starting:
        return LUNASHELL_PROVISIONING_STATE_STARTING;
    case LUNASHELL::ProvisioningState::WaitingForApp:
        return LUNASHELL_PROVISIONING_STATE_WAITING_FOR_APP;
    case LUNASHELL::ProvisioningState::CredentialsReceived:
        return LUNASHELL_PROVISIONING_STATE_CREDENTIALS_RECEIVED;
    case LUNASHELL::ProvisioningState::Connecting:
        return LUNASHELL_PROVISIONING_STATE_CONNECTING;
    case LUNASHELL::ProvisioningState::Success:
        return LUNASHELL_PROVISIONING_STATE_SUCCESS;
    case LUNASHELL::ProvisioningState::Failed:
        return LUNASHELL_PROVISIONING_STATE_FAILED;
    case LUNASHELL::ProvisioningState::Stopping:
        return LUNASHELL_PROVISIONING_STATE_STOPPING;
    case LUNASHELL::ProvisioningState::Deinitializing:
        return LUNASHELL_PROVISIONING_STATE_DEINITIALIZING;
    case LUNASHELL::ProvisioningState::Stopped:
        return LUNASHELL_PROVISIONING_STATE_STOPPED;
    case LUNASHELL::ProvisioningState::Poisoned:
        return LUNASHELL_PROVISIONING_STATE_POISONED;
    }

    ESP_LOGE(kTag, "unknown provisioning state %d", static_cast<int>(state));
    return LUNASHELL_PROVISIONING_STATE_FAILED;
}

LUNASHELL_provisioning_failure_reason_t to_c_failure_reason(
    LUNASHELL::ProvisioningFailureReason reason
)
{
    switch (reason) {
    case LUNASHELL::ProvisioningFailureReason::None:
        return LUNASHELL_PROVISIONING_FAILURE_NONE;
    case LUNASHELL::ProvisioningFailureReason::Authentication:
        return LUNASHELL_PROVISIONING_FAILURE_AUTHENTICATION;
    case LUNASHELL::ProvisioningFailureReason::AccessPointNotFound:
        return LUNASHELL_PROVISIONING_FAILURE_ACCESS_POINT_NOT_FOUND;
    case LUNASHELL::ProvisioningFailureReason::Unknown:
        return LUNASHELL_PROVISIONING_FAILURE_UNKNOWN;
    }

    ESP_LOGE(
        kTag,
        "unknown provisioning failure reason %d",
        static_cast<int>(reason)
    );
    return LUNASHELL_PROVISIONING_FAILURE_UNKNOWN;
}
}

namespace LUNASHELL {

ProvisioningService &ProvisioningService::instance()
{
    static ProvisioningService service;
    return service;
}

esp_err_t ProvisioningService::initialize()
{
    char service_name[sizeof(_service_name)]{};
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_poisoned) {
            return _last_error == ESP_OK
                       ? ESP_ERR_INVALID_STATE
                       : _last_error;
        }
        if (_initialized) {
            return ESP_OK;
        }
        if (_operation != Operation::None) {
            return ESP_ERR_INVALID_STATE;
        }

        const esp_err_t identity_error = rebuildIdentity();
        if (identity_error != ESP_OK) {
            _last_error = identity_error;
            _state = ProvisioningState::Failed;
            ESP_LOGE(
                kTag,
                "identity initialization failed: %s",
                esp_err_to_name(identity_error)
            );
            return identity_error;
        }
        std::snprintf(
            service_name,
            sizeof(service_name),
            "%s",
            _service_name
        );
        _operation = Operation::Initialize;
    }

    network_prov_mgr_config_t config = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BT,
        .app_event_handler = {
            .event_cb = &ProvisioningService::onEvent,
            .user_data = this,
        },
        .network_prov_wifi_conn_cfg = {
            .wifi_conn_attempts = 5,
        },
    };

    // The manager invokes the application callback synchronously from init.
    // Never hold the service mutex across this call.
    esp_err_t error = LUNASHELL_liveness_watch_begin(
        LUNASHELL_LIVENESS_PROVISIONING
    );
    if (error == ESP_OK) {
        const esp_err_t init_error = network_prov_mgr_init(config);
        error = LUNASHELL_liveness_watch_end(
            LUNASHELL_LIVENESS_PROVISIONING,
            init_error
        );
    }
    {
        std::lock_guard<std::mutex> guard(_mutex);
        _operation = Operation::None;
        _last_error = error;
        if (error == ESP_OK) {
            _initialized = true;
            _running = false;
            _state = ProvisioningState::Idle;
        } else {
            _initialized = false;
            _running = false;
            _state = ProvisioningState::Failed;
        }
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "manager init failed: %s", esp_err_to_name(error));
        return error;
    }

    ESP_LOGI(kTag, "BLE provisioning ready as %s", service_name);
    return ESP_OK;
}

esp_err_t ProvisioningService::start()
{
    bool restart_failed_session = false;
    char service_name[sizeof(_service_name)]{};
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_poisoned) {
            return _last_error == ESP_OK
                       ? ESP_ERR_INVALID_STATE
                       : _last_error;
        }
        if (!_initialized || _operation != Operation::None) {
            return ESP_ERR_INVALID_STATE;
        }
        if (_running && _state != ProvisioningState::Failed) {
            return ESP_OK;
        }

        restart_failed_session = _running;
        _operation = Operation::Start;
        _failure_reason = ProvisioningFailureReason::None;
        _state = restart_failed_session
                     ? ProvisioningState::Stopping
                     : ProvisioningState::Starting;
        std::snprintf(
            service_name,
            sizeof(service_name),
            "%s",
            _service_name
        );
    }

    if (restart_failed_session) {
        // The Espressif stop API and its wait path both contain unbounded
        // waits. Run them away from the caller and bound the entire operation.
        esp_err_t stop_error = run_manager_operation_with_deadline(
            ManagerOperation::Stop,
            "failed-session restart"
        );
        bool poisoned = false;
        {
            std::lock_guard<std::mutex> guard(_mutex);
            if (stop_error == ESP_OK && _running) {
                // A completed manager wait must have emitted NETWORK_PROV_END.
                // Continuing would reuse resources with indeterminate ownership.
                stop_error = ESP_ERR_INVALID_STATE;
            }
            if (stop_error != ESP_OK) {
                poisoned = stop_error != ESP_ERR_NO_MEM;
                _operation = Operation::None;
                _last_error = stop_error;
                _poisoned = poisoned;
                _state = poisoned
                             ? ProvisioningState::Poisoned
                             : ProvisioningState::Failed;
            } else {
                _state = ProvisioningState::Starting;
            }
        }
        if (stop_error != ESP_OK) {
            publish_manager_stop_failure(
                "failed-session restart",
                stop_error,
                service_name,
                poisoned
            );
            return stop_error;
        }
    }

    esp_err_t error =
        LUNASHELL::radio_memory::validateBleControllerDma("BLE provisioning");
    if (error == ESP_OK) {
        error = LUNASHELL_liveness_watch_begin(
            LUNASHELL_LIVENESS_PROVISIONING
        );
        if (error == ESP_OK) {
            const esp_err_t start_error =
                network_prov_mgr_start_provisioning(
                    NETWORK_PROV_SECURITY_1,
                    nullptr,
                    service_name,
                    nullptr
                );
            error = LUNASHELL_liveness_watch_end(
                LUNASHELL_LIVENESS_PROVISIONING,
                start_error
            );
        }
    }
    {
        std::lock_guard<std::mutex> guard(_mutex);
        _operation = Operation::None;
        _last_error = error;
        if (error == ESP_OK) {
            _running = true;
            if (_state == ProvisioningState::Starting ||
                _state == ProvisioningState::Stopped) {
                _state = ProvisioningState::WaitingForApp;
            }
        } else {
            _running = false;
            _state = ProvisioningState::Failed;
        }
    }
    if (error != ESP_OK) {
        ESP_LOGE(
            kTag,
            "BLE provisioning start failed: %s",
            esp_err_to_name(error)
        );
    }
    return error;
}

esp_err_t ProvisioningService::stop()
{
    char service_name[sizeof(_service_name)]{};
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_poisoned) {
            return _last_error == ESP_OK
                       ? ESP_ERR_INVALID_STATE
                       : _last_error;
        }
        if (!_initialized || _operation != Operation::None) {
            ESP_LOGE(
                kTag,
                "stop rejected outside an initialized idle lifecycle"
            );
            return ESP_ERR_INVALID_STATE;
        }
        if (!_running) {
            _state = ProvisioningState::Stopped;
            return ESP_OK;
        }
        _operation = Operation::Stop;
        _state = ProvisioningState::Stopping;
        std::snprintf(
            service_name,
            sizeof(service_name),
            "%s",
            _service_name
        );
    }

    // Stop can emit NETWORK_PROV_END; do not hold _mutex while it does. The
    // manager implementation can otherwise wait forever, so isolate and bound
    // the whole stop transaction.
    esp_err_t error = run_manager_operation_with_deadline(
        ManagerOperation::Stop,
        "explicit stop"
    );
    bool poisoned = false;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (error == ESP_OK && _running) {
            error = ESP_ERR_INVALID_STATE;
        }
        _operation = Operation::None;
        _last_error = error;
        if (error == ESP_OK) {
            if (_state != ProvisioningState::Success) {
                _state = ProvisioningState::Stopped;
            }
        } else {
            poisoned = error != ESP_ERR_NO_MEM;
            _poisoned = poisoned;
            _state = poisoned
                         ? ProvisioningState::Poisoned
                         : ProvisioningState::Failed;
        }
    }
    if (error != ESP_OK) {
        publish_manager_stop_failure(
            "explicit stop",
            error,
            service_name,
            poisoned
        );
    }
    return error;
}

esp_err_t ProvisioningService::deinitializeForBleTelemetry()
{
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_poisoned) {
            return _last_error == ESP_OK
                       ? ESP_ERR_INVALID_STATE
                       : _last_error;
        }
        if (_operation != Operation::None) {
            return ESP_ERR_INVALID_STATE;
        }
        if (!_initialized) {
            return ESP_OK;
        }
        _operation = Operation::Deinitialize;
        _state = ProvisioningState::Deinitializing;
    }

    // Deinit may wait on manager locks and event-loop delivery. Run it away
    // from the caller with the same bounded ownership policy as stop.
    const esp_err_t error = run_manager_operation_with_deadline(
        ManagerOperation::Deinitialize,
        "manager deinitialize"
    );
    {
        std::lock_guard<std::mutex> guard(_mutex);
        _operation = Operation::None;
        _last_error = error;
        if (error == ESP_OK) {
            _running = false;
            _initialized = false;
            _state = ProvisioningState::Stopped;
        } else {
            // Manager ownership is indeterminate after a failed deinit.
            // Keep telemetry blocked for the rest of this boot.
            _initialized = true;
            _poisoned = true;
            _state = ProvisioningState::Poisoned;
        }
    }

    if (error != ESP_OK) {
        LUNASHELL::DeviceSnapshotService::instance().reportBle(
            false,
            error,
            false,
            false,
            0xFFFF,
            0,
            0,
            serviceName(),
            true
        );
        ESP_LOGE(
            kTag,
            "provisioning manager deinit failed; BLE telemetry remains "
            "blocked for this boot (%s)",
            esp_err_to_name(error)
        );
        return error;
    }

    ESP_LOGI(
        kTag,
        "provisioning manager deinitialized; BLE ownership is available "
        "for telemetry"
    );
    return ESP_OK;
}

esp_err_t ProvisioningService::isProvisioned(bool *provisioned) const
{
    if (provisioned == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> guard(_mutex);
    if (!_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return network_prov_mgr_is_wifi_provisioned(provisioned);
}

esp_err_t ProvisioningService::connectionSummary(char *buffer, size_t buffer_size) const
{
    if (buffer == nullptr || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    buffer[0] = '\0';
    wifi_config_t config{};
    const esp_err_t wifi_err = esp_wifi_get_config(WIFI_IF_STA, &config);
    if (wifi_err != ESP_OK || config.sta.ssid[0] == '\0') {
        std::snprintf(buffer, buffer_size, "Wi-Fi is not configured");
        return wifi_err == ESP_OK ? ESP_ERR_NOT_FOUND : wifi_err;
    }
    const int ssid_length = static_cast<int>(sizeof(config.sta.ssid));

    esp_netif_ip_info_t ip_info{};
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) {
        std::snprintf(
            buffer,
            buffer_size,
            "Network: %.*s\nInterface unavailable",
            ssid_length,
            reinterpret_cast<const char *>(config.sta.ssid)
        );
        ESP_LOGE(kTag, "connection summary: station netif not found");
        return ESP_ERR_NOT_FOUND;
    }
    const esp_err_t ip_err = esp_netif_get_ip_info(netif, &ip_info);
    if (ip_err != ESP_OK) {
        std::snprintf(
            buffer,
            buffer_size,
            "Network: %.*s\nIP address unavailable",
            ssid_length,
            reinterpret_cast<const char *>(config.sta.ssid)
        );
        ESP_LOGE(
            kTag,
            "connection summary: IP query failed (%s)",
            esp_err_to_name(ip_err)
        );
        return ip_err;
    }
    if (ip_info.ip.addr == 0) {
        std::snprintf(
            buffer,
            buffer_size,
            "Network: %.*s\nIP address pending",
            ssid_length,
            reinterpret_cast<const char *>(config.sta.ssid)
        );
        return ESP_ERR_NOT_FOUND;
    }
    std::snprintf(
        buffer,
        buffer_size,
        "Network: %.*s\nIP: " IPSTR,
        ssid_length,
        reinterpret_cast<const char *>(config.sta.ssid),
        IP2STR(&ip_info.ip)
    );
    return ESP_OK;
}

ProvisioningState ProvisioningService::state() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _state;
}

ProvisioningFailureReason ProvisioningService::failureReason() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _failure_reason;
}

bool ProvisioningService::isInitialized() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _initialized || _operation != Operation::None;
}

bool ProvisioningService::isRunning() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _running;
}

const char *ProvisioningService::serviceName() const
{
    return _service_name;
}

const char *ProvisioningService::qrPayload() const
{
    return _qr_payload;
}

void ProvisioningService::onEvent(void *user_data, network_prov_cb_event_t event, void *event_data)
{
    auto *service = static_cast<ProvisioningService *>(user_data);
    if (service != nullptr) {
        service->handleEvent(event, event_data);
    }
}

void ProvisioningService::handleEvent(network_prov_cb_event_t event, void *event_data)
{
    bool publish_active = false;
    bool publish_stopped = false;
    bool ended_after_success = false;
    char service_name[sizeof(_service_name)]{};
    {
        std::lock_guard<std::mutex> guard(_mutex);
        std::snprintf(
            service_name,
            sizeof(service_name),
            "%s",
            _service_name
        );
        if (_poisoned) {
            if (event == NETWORK_PROV_END || event == NETWORK_PROV_DEINIT) {
                _running = false;
            }
            ESP_LOGE(
                kTag,
                "manager event %d arrived after lifecycle poison; state "
                "remains poisoned (%s)",
                static_cast<int>(event),
                esp_err_to_name(_last_error)
            );
            return;
        }
        switch (event) {
        case NETWORK_PROV_INIT:
            break;
        case NETWORK_PROV_START:
            _running = true;
            _state = ProvisioningState::WaitingForApp;
            _failure_reason = ProvisioningFailureReason::None;
            publish_active = true;
            break;
        case NETWORK_PROV_WIFI_CRED_RECV:
            _state = ProvisioningState::CredentialsReceived;
            _failure_reason = ProvisioningFailureReason::None;
            break;
        case NETWORK_PROV_SET_WIFI_STA_CONFIG:
            _state = ProvisioningState::Connecting;
            break;
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            _state = ProvisioningState::Success;
            _failure_reason = ProvisioningFailureReason::None;
            break;
        case NETWORK_PROV_WIFI_CRED_FAIL: {
            _state = ProvisioningState::Failed;
            if (event_data == nullptr) {
                _failure_reason = ProvisioningFailureReason::Unknown;
                ESP_LOGE(
                    kTag,
                    "Wi-Fi credential failure omitted its reason"
                );
                break;
            }
            const auto reason =
                *static_cast<network_prov_wifi_sta_fail_reason_t *>(event_data);
            switch (reason) {
            case NETWORK_PROV_WIFI_STA_AUTH_ERROR:
                _failure_reason = ProvisioningFailureReason::Authentication;
                ESP_LOGE(kTag, "Wi-Fi authentication failed");
                break;
            case NETWORK_PROV_WIFI_STA_AP_NOT_FOUND:
                _failure_reason =
                    ProvisioningFailureReason::AccessPointNotFound;
                ESP_LOGE(kTag, "Wi-Fi access point was not found");
                break;
            default:
                _failure_reason = ProvisioningFailureReason::Unknown;
                ESP_LOGE(
                    kTag,
                    "Wi-Fi provisioning failed with unknown reason %d",
                    static_cast<int>(reason)
                );
                break;
            }
            break;
        }
        case NETWORK_PROV_END:
            ended_after_success = _state == ProvisioningState::Success;
            _running = false;
            if (!ended_after_success &&
                _state != ProvisioningState::Deinitializing) {
                _state = ProvisioningState::Stopped;
            }
            publish_stopped = true;
            break;
        case NETWORK_PROV_DEINIT:
            _initialized = false;
            _running = false;
            if (_operation != Operation::Deinitialize) {
                _state = ProvisioningState::Stopped;
            }
            publish_stopped = true;
            break;
        default:
            break;
        }
    }

    if (publish_active) {
        LUNASHELL::DeviceSnapshotService::instance().reportBle(
            true,
            ESP_OK,
            true,
            false,
            0xFFFF,
            0,
            0,
            service_name,
            true
        );
    }
    if (publish_stopped) {
        LUNASHELL::DeviceSnapshotService::instance().reportBle(
            false,
            ESP_ERR_INVALID_STATE,
            false,
            false,
            0xFFFF,
            0,
            0,
            service_name,
            false
        );
    }
    if (event == NETWORK_PROV_END && ended_after_success) {
        ESP_LOGI(
            kTag,
            "provisioning completed; telemetry stays off until the explicit "
            "radio handoff deinitializes the provisioning manager"
        );
    }
}

esp_err_t ProvisioningService::rebuildIdentity()
{
    uint8_t mac[6]{};
    const esp_err_t error = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (error != ESP_OK) {
        _service_name[0] = '\0';
        _qr_payload[0] = '\0';
        return error;
    }
    std::snprintf(
        _service_name,
        sizeof(_service_name),
        "PROV_DEVICE-%02X%02X",
        mac[4],
        mac[5]
    );
    std::snprintf(
        _qr_payload,
        sizeof(_qr_payload),
        "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"\",\"transport\":\"ble\",\"security\":1}",
        _service_name);
    return ESP_OK;
}

}  // namespace LUNASHELL

extern "C" esp_err_t LUNASHELL_provisioning_init(void)
{
    return LUNASHELL::ProvisioningService::instance().initialize();
}

extern "C" esp_err_t LUNASHELL_provisioning_start(void)
{
    return LUNASHELL::ProvisioningService::instance().start();
}

extern "C" esp_err_t LUNASHELL_provisioning_stop(void)
{
    return LUNASHELL::ProvisioningService::instance().stop();
}

extern "C" esp_err_t LUNASHELL_provisioning_deinit_for_ble(void)
{
    return LUNASHELL::ProvisioningService::instance().deinitializeForBleTelemetry();
}

extern "C" bool LUNASHELL_provisioning_initialized(void)
{
    return LUNASHELL::ProvisioningService::instance().isInitialized();
}

extern "C" bool LUNASHELL_provisioning_active(void)
{
    return LUNASHELL::ProvisioningService::instance().isRunning();
}

extern "C" LUNASHELL_provisioning_state_t LUNASHELL_provisioning_state(void)
{
    return to_c_state(LUNASHELL::ProvisioningService::instance().state());
}

extern "C" LUNASHELL_provisioning_failure_reason_t
LUNASHELL_provisioning_failure_reason(void)
{
    return to_c_failure_reason(
        LUNASHELL::ProvisioningService::instance().failureReason()
    );
}

extern "C" esp_err_t LUNASHELL_provisioning_connection_summary(
    char *buffer,
    size_t buffer_size
)
{
    return LUNASHELL::ProvisioningService::instance().connectionSummary(
        buffer,
        buffer_size
    );
}

extern "C" const char *LUNASHELL_provisioning_service_name(void)
{
    return LUNASHELL::ProvisioningService::instance().serviceName();
}

extern "C" const char *LUNASHELL_provisioning_qr_payload(void)
{
    return LUNASHELL::ProvisioningService::instance().qrPayload();
}

extern "C" const char *LUNASHELL_provisioning_pop(void)
{
    return "";
}
