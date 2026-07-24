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
    Stopped,
};

/** BLE-only Espressif network provisioning facade for Wi-Fi credentials. */
class ProvisioningService final {
public:
    static ProvisioningService &instance();

    esp_err_t initialize();
    esp_err_t start();
    esp_err_t stop();
    esp_err_t isProvisioned(bool *provisioned) const;
    esp_err_t connectionSummary(char *buffer, size_t buffer_size) const;

    ProvisioningState state() const;
    const char *serviceName() const;
    const char *qrPayload() const;

private:
    ProvisioningService() = default;
    ProvisioningService(const ProvisioningService &) = delete;
    ProvisioningService &operator=(const ProvisioningService &) = delete;

    static void onEvent(void *user_data, network_prov_cb_event_t event, void *event_data);
    void handleEvent(network_prov_cb_event_t event, void *event_data);
    void rebuildIdentity();

    mutable std::mutex _mutex;
    bool _initialized = false;
    bool _running = false;
    ProvisioningState _state = ProvisioningState::Idle;
    char _service_name[24]{};
    char _qr_payload[192]{};
};

}  // namespace qsee
