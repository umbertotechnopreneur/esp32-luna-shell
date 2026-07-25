#pragma once

// Provisioning entry points for Wi-Fi onboarding and pairing.
// Exposes the minimal API used by the app and setup flows.

#include <cstddef>
#include <mutex>

#include "esp_err.h"
#include "wifi_provisioning/manager.h"

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

/** BLE-only Espressif Wi-Fi provisioning facade. */
class ProvisioningService final {
public:
    static ProvisioningService &instance();

    /** Create the manager without starting its BLE transport. */
    esp_err_t initialize();
    /** Start or reuse one Security 1 provisioning session. */
    esp_err_t start();
    /** Deinitialize synchronously so the next retry starts cleanly. */
    esp_err_t stop();
    /** Report whether provisioning currently owns the BLE transport. */
    bool transportActive() const;
    esp_err_t isProvisioned(bool *provisioned) const;
    esp_err_t connectionSummary(char *buffer, size_t buffer_size) const;

    ProvisioningState state() const;
    const char *serviceName() const;
    const char *qrPayload() const;

private:
    ProvisioningService() = default;
    ProvisioningService(const ProvisioningService &) = delete;
    ProvisioningService &operator=(const ProvisioningService &) = delete;

    static void onEvent(void *user_data, wifi_prov_cb_event_t event, void *event_data);
    void handleEvent(wifi_prov_cb_event_t event, void *event_data);
    void rebuildIdentity();

    mutable std::mutex _mutex;
    bool _initialized = false;
    bool _running = false;
    bool _transport_active = false;
    bool _initializing = false;
    bool _starting = false;
    bool _stopping = false;
    bool _credentials_succeeded = false;
    ProvisioningState _state = ProvisioningState::Idle;
    char _service_name[24]{};
    char _qr_payload[192]{};
};

}  // namespace qsee
