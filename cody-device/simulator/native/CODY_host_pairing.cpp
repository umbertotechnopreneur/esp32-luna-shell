#include "CODY_host_pairing.hpp"

#include <array>
#include <cstring>
#include <mutex>

namespace {
std::mutex s_mutex;
bool s_configured = false;
bool s_running = false;
esp_err_t s_start_error = ESP_ERR_NOT_SUPPORTED;
LUNASHELL_radio_cleanup_result_t s_cleanup{ESP_ERR_NOT_SUPPORTED, ESP_ERR_NOT_SUPPORTED};
LUNASHELL::ProvisioningSnapshot s_ready{};
std::array<char, 256> s_qr{};
}

// start_error: Explicit outcome of this synthetic radio scenario.
// cleanup: Independent explicit cleanup and DMA outcomes.
// ready: Synthetic provisioning state owned by this scenario.
// qr: Bounded payload explicitly marked SIMULATED and impossible to use as device pairing.
// Exception: Rejects malformed or active replacement before mutating the scenario.
esp_err_t CODY::host_onboarding::configure_pairing(esp_err_t start_error,
    LUNASHELL_radio_cleanup_result_t cleanup, const LUNASHELL::ProvisioningSnapshot &ready, const char *qr)
{
    if (!qr || std::strncmp(qr, "SIMULATED:", 10) != 0) return ESP_ERR_INVALID_ARG;
    size_t length = 0;
    while (length < s_qr.size() && qr[length] != '\0') ++length;
    if (length == s_qr.size()) return ESP_ERR_INVALID_SIZE;
    if (start_error == ESP_OK && (!ready.data_valid || !ready.initialized || !ready.running || ready.session_id == 0))
        return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_running) return ESP_ERR_INVALID_STATE;
    s_start_error = start_error;
    s_cleanup = cleanup;
    s_ready = ready;
    s_qr.fill('\0');
    std::memcpy(s_qr.data(), qr, length);
    s_configured = true;
    return ESP_OK;
}

#if defined(_WIN32)
#define CODY_HOST_PAIRING_API extern "C" __declspec(dllexport)
#else
#define CODY_HOST_PAIRING_API extern "C" __attribute__((visibility("default")))
#endif

// mode: 1 simulated QR ready, 2 start error, 3 start timeout.
// Exception: Preserves scenario validation and active-session errors.
CODY_HOST_PAIRING_API esp_err_t cody_host_pairing_seed_scenario(int mode)
{
    if (mode < 1 || mode > 3) return ESP_ERR_INVALID_ARG;
    LUNASHELL::ProvisioningSnapshot ready{};
    ready.health = LUNASHELL::DeviceHealth::Ready;
    ready.data_valid = true;
    ready.initialized = ready.running = true;
    ready.session_id = 1;
    ready.state = LUNASHELL::ProvisioningState::WaitingForApp;
    const auto start_error = mode == 1 ? ESP_OK : (mode == 2 ? ESP_FAIL : ESP_ERR_TIMEOUT);
    return CODY::host_onboarding::configure_pairing(start_error, {ESP_OK, ESP_OK}, ready,
        "SIMULATED:CODY-BLE-PAIRING");
}

// No parameters: starts only an explicitly configured synthetic radio scenario.
// Exception: Returns the seeded scenario or observation error; never touches host Wi-Fi/BLE.
extern "C" esp_err_t LUNASHELL_radios_start_pairing(void)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_configured) return ESP_ERR_NOT_SUPPORTED;
    if (s_running) return ESP_ERR_INVALID_STATE;
    if (s_start_error != ESP_OK) return s_start_error;
    LUNASHELL::ConnectivityObservation observation{};
    const auto error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    if (error != ESP_OK) return error;
    LUNASHELL::DeviceSnapshotService::instance().reportProvisioning(s_ready);
    s_running = true;
    return ESP_OK;
}

// restore_telemetry: Original controller's explicit telemetry-restoration request.
// Exception: Returns configured cleanup errors without claiming resources closed.
extern "C" LUNASHELL_radio_cleanup_result_t LUNASHELL_radios_end_pairing(bool restore_telemetry)
{
    (void)restore_telemetry;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_configured) return {ESP_ERR_NOT_SUPPORTED, ESP_ERR_NOT_SUPPORTED};
    if (s_cleanup.cleanup_error == ESP_OK) {
        auto closed = s_ready;
        closed.initialized = false;
        closed.running = false;
        closed.state = LUNASHELL::ProvisioningState::Idle;
        LUNASHELL::DeviceSnapshotService::instance().reportProvisioning(closed);
        s_running = false;
    }
    return s_cleanup;
}

// buffer: Caller-owned destination for a synthetic QR payload.
// size: Exact destination capacity including its terminator.
// session_id: Current synthetic provisioning session identity.
// Exception: Rejects stale sessions, missing scenario and insufficient space.
extern "C" esp_err_t LUNASHELL_provisioning_copy_qr(char *buffer, size_t size, uint32_t session_id)
{
    if (!buffer || size == 0) return ESP_ERR_INVALID_ARG;
    buffer[0] = '\0';
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_configured || !s_running || session_id == 0 || session_id != s_ready.session_id)
        return ESP_ERR_INVALID_STATE;
    const auto length = std::strlen(s_qr.data());
    if (length + 1 > size) return ESP_ERR_INVALID_SIZE;
    std::memcpy(buffer, s_qr.data(), length + 1);
    return ESP_OK;
}
