#pragma once

#include <cstddef>
#include <mutex>

#include "esp_err.h"
#include "network_provisioning/manager.h"

namespace qsee {

enum class ProvisioningState {
    Idle,
    Starting,
    WaitingForApp,
    CredentialsReceived,
    Connecting,
    Success,
    Failed,
    Stopping,
    Deinitializing,
    Stopped,
    Poisoned,
};

enum class ProvisioningFailureReason {
    None,
    Authentication,
    AccessPointNotFound,
    Unknown,
};

/** BLE-only Espressif network provisioning facade for Wi-Fi credentials. */
class ProvisioningService final {
public:
    static ProvisioningService &instance();

    esp_err_t initialize();
    esp_err_t start();
    esp_err_t stop();
    esp_err_t deinitializeForBleTelemetry();
    esp_err_t isProvisioned(bool *provisioned) const;
    esp_err_t connectionSummary(char *buffer, size_t buffer_size) const;

    bool isInitialized() const;
    bool isRunning() const;
    ProvisioningState state() const;
    ProvisioningFailureReason failureReason() const;
    const char *serviceName() const;
    const char *qrPayload() const;

private:
    ProvisioningService() = default;
    ProvisioningService(const ProvisioningService &) = delete;
    ProvisioningService &operator=(const ProvisioningService &) = delete;

    static void onEvent(void *user_data, network_prov_cb_event_t event, void *event_data);
    void handleEvent(network_prov_cb_event_t event, void *event_data);
    esp_err_t rebuildIdentity();

    enum class Operation {
        None,
        Initialize,
        Start,
        Stop,
        Deinitialize,
    };

    mutable std::mutex _mutex;
    bool _initialized = false;
    bool _running = false;
    bool _poisoned = false;
    Operation _operation = Operation::None;
    esp_err_t _last_error = ESP_OK;
    ProvisioningState _state = ProvisioningState::Idle;
    ProvisioningFailureReason _failure_reason = ProvisioningFailureReason::None;
    char _service_name[24]{};
    char _qr_payload[192]{};
};

}  // namespace qsee
