#include "LUNASHELL_platform.h"
#include "LUNASHELL_device_snapshot.hpp"

#include <mutex>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_LUNASHELL_BUILD_ENABLE_PROVISIONING && CONFIG_LUNASHELL_BUILD_ENABLE_WIFI && CONFIG_LUNASHELL_BUILD_ENABLE_BLUETOOTH

namespace {
constexpr char kTag[] = "LUNASHELL_pairing";
constexpr int64_t kSessionTimeoutUs = 5LL * 60LL * 1000000LL;
constexpr uint32_t kPollMs = 250;
constexpr size_t kWorkerStackSize = 4096;
using State = LUNASHELL::PairingSessionState;
using Reason = LUNASHELL::ProvisioningFailureReason;

enum class Command : uint8_t {
    None,
    Start,
    Retry,
    Cancel,
};
std::mutex s_mutex;
StaticTask_t s_worker_control{};
StackType_t s_worker_stack[kWorkerStackSize]{};
TaskHandle_t s_worker = nullptr;
Command s_pending = Command::None;
bool s_busy = false;
LUNASHELL::PairingSessionSnapshot s_session{};

// state: Confirmed controller transition, or an explicitly pending operation.
// error: Original operation error; a failed cleanup has its own retained field.
void publishLocked(State state, esp_err_t error)
{
    s_session.state = state;
    s_session.last_error = error;
    s_session.changed_at_us = esp_timer_get_time();
    s_session.last_attempt_at_us = s_session.changed_at_us;
    s_session.command_pending = s_busy || s_pending != Command::None;
    s_session.data_valid = true;
    s_session.data_stale = false;
    s_session.health = error == ESP_OK
        ? LUNASHELL::DeviceHealth::Ready : LUNASHELL::DeviceHealth::Degraded;
    if (error == ESP_OK) {
        s_session.last_success_at_us = s_session.changed_at_us;
    }
    LUNASHELL::DeviceSnapshotService::instance().reportPairingSession(s_session);
}

// state: Controller state to classify as an active, deadline-bound attempt.
bool awaitingConnection(State state)
{
    return state == State::ReadyToScan || state == State::Connecting;
}

// terminal: Terminal state requested after confirmed resource cleanup.
// primary_error: Cause of closure, preserved if cleanup fails too.
// reason: Manager's actual credential failure classification, when available.
// restore_telemetry: True only after provisioning success and a usable Wi-Fi IP.
void closeSession(State terminal, esp_err_t primary_error, Reason reason, bool restore_telemetry)
{
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        s_busy = true;
        s_session.failure_reason = reason;
        publishLocked(restore_telemetry ? State::Completing : State::Cancelling, primary_error);
    }
    const esp_err_t cleanup_error = LUNASHELL_radios_end_pairing(restore_telemetry);
    if (cleanup_error != ESP_OK) {
        ESP_LOGE(kTag, "Pairing cleanup failed (%s), primary result %s",
                 esp_err_to_name(cleanup_error), esp_err_to_name(primary_error));
    }
    std::lock_guard<std::mutex> guard(s_mutex);
    s_busy = false;
    s_session.cleanup_error = cleanup_error;
    s_session.deadline_at_us = 0;
    const esp_err_t result = primary_error == ESP_OK ? cleanup_error : primary_error;
    if (s_pending == Command::Cancel && cleanup_error == ESP_OK) {
        publishLocked(State::Cancelling, result);
    } else {
        publishLocked(cleanup_error == ESP_OK ? terminal : State::Failed, result);
    }
}

void startSession()
{
    LUNASHELL::ConnectivityObservation observation{};
    esp_err_t error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    if (error == ESP_OK && observation.operating_state != LUNASHELL::DeviceOperatingState::Active) {
        error = ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "Pairing cannot start outside Active power state (%s)", esp_err_to_name(error));
    }
    if (error == ESP_OK) {
        error = LUNASHELL_radios_start_pairing();
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Pairing start failed (%s)", esp_err_to_name(error));
        closeSession(State::Failed, error, Reason::None, false);
        return;
    }
    error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    if (error != ESP_OK || !observation.radios.provisioning.running ||
        observation.radios.provisioning.session_id == 0) {
        closeSession(State::Failed, error == ESP_OK ? ESP_ERR_INVALID_STATE : error, Reason::None, false);
        return;
    }
    std::lock_guard<std::mutex> guard(s_mutex);
    s_busy = false;
    s_session.provisioning_session_id = observation.radios.provisioning.session_id;
    s_session.deadline_at_us = esp_timer_get_time() + kSessionTimeoutUs;
    State state = State::ReadyToScan;
    if (observation.radios.provisioning.state == LUNASHELL::ProvisioningState::CredentialsReceived ||
        observation.radios.provisioning.state == LUNASHELL::ProvisioningState::Connecting) {
        state = State::Connecting;
    }
    publishLocked(s_pending == Command::Cancel ? State::Cancelling : state, ESP_OK);
}

void observeSession()
{
    LUNASHELL::PairingSessionSnapshot session{};
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (s_busy || s_pending != Command::None || !awaitingConnection(s_session.state)) {
            return;
        }
        session = s_session;
    }
    LUNASHELL::ConnectivityObservation observation{};
    const esp_err_t read_error = LUNASHELL::DeviceSnapshotService::instance().readConnectivityObservation(observation);
    if (read_error != ESP_OK) {
        closeSession(State::Failed, read_error, Reason::None, false);
        return;
    }
    if (observation.operating_state != LUNASHELL::DeviceOperatingState::Active) {
        closeSession(State::Cancelled, ESP_OK, Reason::None, false);
        return;
    }
    const auto &provisioning = observation.radios.provisioning;
    if (!provisioning.data_valid || provisioning.session_id != session.provisioning_session_id) {
        closeSession(State::Failed, ESP_ERR_INVALID_STATE, Reason::Unknown, false);
        return;
    }
    using ProvisioningState = LUNASHELL::ProvisioningState;
    if (provisioning.state == ProvisioningState::Failed || provisioning.state == ProvisioningState::Poisoned) {
        closeSession(State::Failed, provisioning.last_error == ESP_OK ? ESP_ERR_INVALID_STATE : provisioning.last_error,
                     provisioning.failure_reason, false);
        return;
    }
    const auto &wifi = observation.radios.wifi;
    const bool online = wifi.health == LUNASHELL::DeviceHealth::Ready &&
        wifi.data_valid && !wifi.data_stale && wifi.last_error == ESP_OK &&
        wifi.connected && wifi.ip_acquired && wifi.ip_address[0] != '\0';
    if (provisioning.state == ProvisioningState::Success && online) {
        closeSession(State::Online, ESP_OK, Reason::None, true);
        return;
    }
    if (esp_timer_get_time() >= session.deadline_at_us) {
        ESP_LOGW(kTag, "Pairing attempt %lu timed out (%s)",
                 static_cast<unsigned long>(session.attempt_id), esp_err_to_name(ESP_ERR_TIMEOUT));
        closeSession(State::TimedOut, ESP_ERR_TIMEOUT, Reason::None, false);
        return;
    }
    if (!provisioning.running) {
        closeSession(State::Failed, ESP_ERR_INVALID_STATE, Reason::Unknown, false);
        return;
    }
    if (provisioning.state == ProvisioningState::CredentialsReceived ||
        provisioning.state == ProvisioningState::Connecting) {
        std::lock_guard<std::mutex> guard(s_mutex);
        if (s_pending == Command::None && s_session.state != State::Connecting) {
            publishLocked(State::Connecting, ESP_OK);
        }
    }
}

// context: Unused singleton task context; all owned state is protected by s_mutex.
void pairingWorker(void *context)
{
    (void)context;
    for (;;) {
        Command command = Command::None;
        {
            std::lock_guard<std::mutex> guard(s_mutex);
            command = s_pending;
            s_pending = Command::None;
        }
        switch (command) {
        case Command::Start:
        case Command::Retry:
            startSession();
            break;
        case Command::Cancel:
            closeSession(State::Cancelled, ESP_OK, Reason::None, false);
            break;
        case Command::None:
            observeSession();
            break;
        }
        bool active = false;
        {
            std::lock_guard<std::mutex> guard(s_mutex);
            active = awaitingConnection(s_session.state) || s_pending != Command::None;
        }
        ulTaskNotifyTake(pdTRUE, active ? pdMS_TO_TICKS(kPollMs) : portMAX_DELAY);
    }
}

// action: Rejected semantic action, included in the log without any secret payload.
// attempt: Attempt identity supplied by the caller.
// error: Exact rejection returned to the caller.
esp_err_t reject(LUNASHELL_pairing_action_t action, uint32_t attempt, esp_err_t error)
{
    ESP_LOGE(kTag, "Pairing action %d for attempt %lu rejected (%s)",
             static_cast<int>(action), static_cast<unsigned long>(attempt), esp_err_to_name(error));
    return error;
}
}  // namespace

extern "C" esp_err_t LUNASHELL_pairing_service_init(void)
{
    std::lock_guard<std::mutex> guard(s_mutex);
    if (s_worker != nullptr) {
        return ESP_OK;
    }
    s_worker = xTaskCreateStatic(pairingWorker, "LUNASHELL_pair", kWorkerStackSize,
                                nullptr, 3, s_worker_stack, &s_worker_control);
    if (s_worker == nullptr) {
        ESP_LOGE(kTag, "Pairing worker creation failed (%s)", esp_err_to_name(ESP_ERR_NO_MEM));
        publishLocked(State::Failed, ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    publishLocked(State::Idle, ESP_OK);
    return ESP_OK;
}

// action: Explicit Start, Retry or Cancel action from the active product route.
// expected_attempt_id: Identity read from the canonical pairing session snapshot.
extern "C" esp_err_t LUNASHELL_pairing_request(LUNASHELL_pairing_action_t action, uint32_t expected_attempt_id)
{
    Command command = Command::None;
    switch (action) {
    case LUNASHELL_PAIRING_ACTION_START:
        command = Command::Start;
        break;
    case LUNASHELL_PAIRING_ACTION_RETRY:
        command = Command::Retry;
        break;
    case LUNASHELL_PAIRING_ACTION_CANCEL:
        command = Command::Cancel;
        break;
    default:
        return reject(action, expected_attempt_id, ESP_ERR_INVALID_ARG);
    }
    std::lock_guard<std::mutex> guard(s_mutex);
    if (s_worker == nullptr || expected_attempt_id != s_session.attempt_id) {
        return reject(action, expected_attempt_id, ESP_ERR_INVALID_STATE);
    }
    if (command == Command::Cancel) {
        // Cancellation can be queued during synchronous radio startup/cleanup.
        // The UI waits for Cancelled; it never assumes that admission is cleanup.
        const bool cancellable = s_session.state == State::Starting ||
            awaitingConnection(s_session.state) || s_session.state == State::Completing ||
            (s_session.state == State::Failed && s_session.cleanup_error != ESP_OK);
        if (!cancellable || s_pending == Command::Cancel) {
            return reject(action, expected_attempt_id, ESP_ERR_INVALID_STATE);
        }
        s_pending = command;
        publishLocked(State::Cancelling, ESP_OK);
    } else {
        if (s_busy || s_pending != Command::None || s_session.cleanup_error != ESP_OK) {
            return reject(action, expected_attempt_id, ESP_ERR_INVALID_STATE);
        }
        const bool allowed = command == Command::Retry
            ? (s_session.state == State::Failed || s_session.state == State::TimedOut)
            : (s_session.state == State::Idle || s_session.state == State::Cancelled || s_session.state == State::Online);
        if (!allowed || s_session.attempt_id == UINT32_MAX) {
            return reject(action, expected_attempt_id, ESP_ERR_INVALID_STATE);
        }
        ++s_session.attempt_id;
        s_session.provisioning_session_id = 0;
        s_session.deadline_at_us = 0;
        s_session.failure_reason = Reason::None;
        s_busy = true;
        s_pending = command;
        publishLocked(State::Starting, ESP_OK);
    }
    xTaskNotifyGive(s_worker);
    return ESP_OK;
}

#else
extern "C" esp_err_t LUNASHELL_pairing_service_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

// action: Unused semantic action in a profile without the full provisioning stack.
// expected_attempt_id: Unused attempt identity in this profile.
extern "C" esp_err_t LUNASHELL_pairing_request(LUNASHELL_pairing_action_t action, uint32_t expected_attempt_id)
{
    (void)action;
    (void)expected_attempt_id;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
