/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

#pragma once

#include <cstddef>
#include <mutex>

#include "esp_err.h"
#include "LUNASHELL_provisioning_state.hpp"
#include "network_provisioning/manager.h"

namespace LUNASHELL {

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
    const char *proofOfPossession() const;
    // buffer: Caller-owned destination for the active session's secret QR.
    // buffer_size: Destination capacity, including the final null byte.
    // session_id: Expected snapshot session identity; stale callers are rejected.
    esp_err_t copyQrPayload(char *buffer, size_t buffer_size, uint32_t session_id) const;

private:
    ProvisioningService() = default;
    ProvisioningService(const ProvisioningService &) = delete;
    ProvisioningService &operator=(const ProvisioningService &) = delete;

    static void onEvent(void *user_data, network_prov_cb_event_t event, void *event_data);
    void handleEvent(network_prov_cb_event_t event, void *event_data);
    esp_err_t rebuildIdentity();
    void publishSnapshotLocked();

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
    esp_err_t _wifi_handoff_error = ESP_OK;
    ProvisioningState _state = ProvisioningState::Idle;
    ProvisioningFailureReason _failure_reason = ProvisioningFailureReason::None;
    bool _credentials_succeeded = false;
    uint32_t _session_id = 0;
    uint32_t _event_sequence = 0;
    int64_t _last_success_at_us = 0;
    char _service_name[24]{};
    char _qr_payload[192]{};
    char _pop[17]{};
};

}  // namespace LUNASHELL
