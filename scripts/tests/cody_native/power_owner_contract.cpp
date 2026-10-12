// Executes verbatim firmware owner functions with deterministic peripheral results.
// This proves host control flow, not PMU delivery, frame presentation or power loss.
#include "LUNASHELL_button_hold.hpp"
#include "LUNASHELL_logger.h"
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>

#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)
#define pdMS_TO_TICKS(value) (value)

namespace LUNASHELL {
// requested: Recorder boundary is injected; its real persistence is tested separately.
esp_err_t battery_endurance_power_off(bool requested);
#include "power_health_under_test.inc"
// Only fields consumed by the real predicate are injected; health is copied
// verbatim from the production typed enum, not replaced by integer conventions.
struct BatteryObservation {
    DeviceHealth health = DeviceHealth::Ready;
    bool data_valid = true;
    bool data_stale = false;
    esp_err_t last_error = ESP_OK;
    int64_t last_success_at_us = 1000;
    bool battery_present = true;
    bool usb_present = false;
    bool vbus_good = false;
    int level_percent = 100;
    const char *power_state = "standby";
};
struct DeviceStatusObservation { BatteryObservation battery; };
class DeviceSnapshotService {
public:
    static DeviceSnapshotService &instance();
    esp_err_t readDeviceStatusObservation(DeviceStatusObservation &observation);
};
enum class DeviceOperatingState { Active, Standby };
enum class DeviceStateReason { PowerButton };
class UserSettingsService {
public:
    static UserSettingsService &instance();
    esp_err_t setPowerOffSealed(bool sealed);
};
}
namespace {
enum class Call { Wake, Prepare, SpeechSeal, Standby, SettingsSeal, UsbSeal,
                  Pmu, Wait, UsbOpen, SettingsOpen, SpeechOpen, DisplayError,
                  EnduranceRequest, EnduranceCancel };
std::vector<Call> calls;
LUNASHELL::DeviceStatusObservation canonical_observation;
esp_err_t observation_error;
unsigned observation_reads;
esp_err_t prepare_error, speech_error, standby_error, settings_error, usb_error, pmu_error;
esp_err_t settings_open_error, endurance_error;
bool usb_quiesced;
bool s_shutdown_in_progress, s_shutdown_admission_closed, s_power_long_press_consumed;
esp_err_t s_shutdown_error;
int s_shutdown_admission_mux;
struct { uint32_t power_short_press_count, power_long_press_count; } s_inputs;
LUNASHELL::DeviceOperatingState s_state;
std::atomic<esp_err_t (*)(void)> s_shutdown_prepare;
std::atomic<esp_err_t (*)(esp_err_t)> s_shutdown_failure;
std::atomic<esp_err_t (*)(bool)> s_speech_shutdown;

// No parameters: deterministic test time does not sample any hardware.
int64_t esp_timer_get_time() { return 1000000; }
// time_us: Production gesture timestamp; no movement hardware exists on the host.
void begin_motion_grace(int64_t time_us) { (void)time_us; }
// error: Original result whose production publication is outside this host fixture.
void publish_power(esp_err_t error) { (void)error; }
// ticks: Requested wait is recorded without blocking the host.
void vTaskDelay(uint32_t ticks) { (void)ticks; calls.push_back(Call::Wait); }
// standby: Requested owner transition; only the injected standby error can fail.
// reason: Production typed cause retained by the compiled caller.
esp_err_t set_standby(bool standby, LUNASHELL::DeviceStateReason reason)
{
    (void)reason;
    calls.push_back(standby ? Call::Standby : Call::Wake);
    if (standby && standby_error != ESP_OK) return standby_error;
    s_state = standby ? LUNASHELL::DeviceOperatingState::Standby : LUNASHELL::DeviceOperatingState::Active;
    return ESP_OK;
}
// No parameters: records the synchronous presentation owner boundary.
esp_err_t prepare() { calls.push_back(Call::Prepare); return prepare_error; }
// error: Error passed back to the visible failure owner.
esp_err_t display_error(esp_err_t error) { assert(error != ESP_OK); calls.push_back(Call::DisplayError); return ESP_OK; }
// sealed: Records speech owner admission and restoration separately.
esp_err_t speech(bool sealed) { calls.push_back(sealed ? Call::SpeechSeal : Call::SpeechOpen); return sealed ? speech_error : ESP_OK; }
// No parameters: counts actual PMU command admission without issuing I2C.
esp_err_t LUNASHELL_hardware_request_power_off() { calls.push_back(Call::Pmu); return pmu_error; }
}

// No parameters: one injected canonical observation service exists in the fixture.
LUNASHELL::DeviceSnapshotService &LUNASHELL::DeviceSnapshotService::instance()
{
    static DeviceSnapshotService owner;
    return owner;
}
// observation: Receives injected already-aged telemetry only on a successful read.
esp_err_t LUNASHELL::DeviceSnapshotService::readDeviceStatusObservation(DeviceStatusObservation &observation)
{
    ++observation_reads;
    if (observation_error == ESP_OK) observation = canonical_observation;
    return observation_error;
}

// No parameters: the host settings owner has one service lifetime.
LUNASHELL::UserSettingsService &LUNASHELL::UserSettingsService::instance()
{
    static UserSettingsService owner;
    return owner;
}
// sealed: Injects the persistence owner's admission failure independently from USB.
esp_err_t LUNASHELL::UserSettingsService::setPowerOffSealed(bool sealed)
{
    calls.push_back(sealed ? Call::SettingsSeal : Call::SettingsOpen);
    return sealed ? settings_error : settings_open_error;
}
// requested: Record ordering while injecting optional recorder failure independently.
esp_err_t LUNASHELL::battery_endurance_power_off(bool requested)
{
    calls.push_back(requested ? Call::EnduranceRequest : Call::EnduranceCancel);
    return endurance_error;
}
// sealed: Records logger TX admission independently from delivery.
// status: Reports whether the injected failure concerns ownership or optional USB delivery.
extern "C" esp_err_t LUNASHELL_logger_set_power_off(bool sealed, LUNASHELL_logger_power_off_status_t *status)
{
    calls.push_back(sealed ? Call::UsbSeal : Call::UsbOpen);
    *status = {};
    status->sealed = sealed;
    status->ownership_quiesced = usb_quiesced;
    status->drain_error = sealed ? usb_error : ESP_OK;
    return status->drain_error;
}

// Generated from current production source by the public power-policy check.
#include "power_owner_under_test.inc"

// No parameters: reset injected owner failures and production transaction state.
void reset_case()
{
    calls.clear();
    canonical_observation = {};
    observation_error = ESP_OK;
    observation_reads = 0;
    prepare_error = speech_error = standby_error = settings_error = usb_error = ESP_OK;
    settings_open_error = endurance_error = ESP_OK;
    pmu_error = ESP_FAIL;
    usb_quiesced = true;
    s_shutdown_in_progress = s_shutdown_admission_closed = s_power_long_press_consumed = false;
    s_shutdown_error = ESP_OK;
    s_inputs = {};
    s_state = LUNASHELL::DeviceOperatingState::Active;
    s_shutdown_prepare.store(prepare);
    s_shutdown_failure.store(display_error);
    s_speech_shutdown.store(speech);
}
// call: Owner boundary whose invocation is asserted by a test case.
bool called(Call call) { return std::find(calls.begin(), calls.end(), call) != calls.end(); }

// No parameters: exercise real firmware control functions and real ButtonHold code.
// Exception: Assertion failures retain the violated invariant in native stderr.
int main()
{
    reset_case();
    assert(battery_allows_automatic_standby());
    canonical_observation.battery.usb_present = true;
    canonical_observation.battery.vbus_good = true;
    for (const char *state : {"charging", "standby", "done"}) {
        canonical_observation.battery.power_state = state;
        assert(!battery_allows_automatic_standby());
    }
    // Manual Power retains its independent owner route even with full battery on USB.
    const unsigned reads_before_manual = observation_reads;
    assert(handle_power_key_events(1, 0, ESP_OK) == ESP_OK);
    assert(s_state == LUNASHELL::DeviceOperatingState::Standby);
    assert(handle_power_key_events(1, 0, ESP_OK) == ESP_OK);
    assert(s_state == LUNASHELL::DeviceOperatingState::Active);
    assert(observation_reads == reads_before_manual);
    canonical_observation.battery.battery_present = false;
    assert(!battery_allows_automatic_standby()); // USB-only with no cell.
    reset_case();
    canonical_observation.battery.vbus_good = true;
    assert(!battery_allows_automatic_standby());
    reset_case();
    canonical_observation.battery.usb_present = true;
    assert(!battery_allows_automatic_standby());
    reset_case();
    canonical_observation.battery.battery_present = false;
    assert(!battery_allows_automatic_standby());
    reset_case();
    canonical_observation.battery.data_valid = false;
    assert(!battery_allows_automatic_standby());
    reset_case();
    canonical_observation.battery.data_stale = true;
    assert(!battery_allows_automatic_standby());
    reset_case();
    canonical_observation.battery.last_error = ESP_FAIL;
    assert(!battery_allows_automatic_standby());
    for (const auto health : {LUNASHELL::DeviceHealth::Unknown, LUNASHELL::DeviceHealth::Missing,
                             LUNASHELL::DeviceHealth::Degraded}) {
        reset_case();
        canonical_observation.battery.health = health;
        assert(!battery_allows_automatic_standby());
    }
    reset_case();
    canonical_observation.battery.last_success_at_us = 0;
    assert(!battery_allows_automatic_standby());
    canonical_observation.battery.last_success_at_us = -1;
    assert(!battery_allows_automatic_standby());
    reset_case();
    observation_error = ESP_ERR_TIMEOUT;
    assert(!battery_allows_automatic_standby());
    reset_case();
    assert(handle_power_key_events(1, 0, ESP_OK) == ESP_OK);
    assert(s_state == LUNASHELL::DeviceOperatingState::Standby);
    assert(handle_power_key_events(1, 0, ESP_OK) == ESP_OK);
    assert(s_state == LUNASHELL::DeviceOperatingState::Active);
    reset_case();
    assert(handle_power_key_events(1, 1, ESP_OK) == ESP_OK);
    assert(calls.empty() && s_power_long_press_consumed);
    assert(handle_power_key_events(1, 0, ESP_OK) == ESP_OK && calls.empty());
    reset_case();
    assert(handle_power_key_events(1, 0, ESP_ERR_TIMEOUT) == ESP_ERR_TIMEOUT);
    assert(calls.empty() && s_inputs.power_short_press_count == 0);
    reset_case();
    assert(handle_power_key_events(2, 0, ESP_OK) == ESP_ERR_INVALID_RESPONSE && calls.empty());
    s_inputs.power_short_press_count = UINT32_MAX;
    assert(handle_power_key_events(1, 0, ESP_OK) == ESP_ERR_INVALID_SIZE && calls.empty());

    for (esp_err_t *failure : {&prepare_error, &speech_error, &standby_error, &settings_error}) {
        reset_case();
        *failure = ESP_ERR_INVALID_RESPONSE;
        assert(request_power_off() == ESP_ERR_INVALID_RESPONSE);
        assert(!called(Call::Pmu) && called(Call::DisplayError));
        assert(!s_shutdown_in_progress && !s_shutdown_admission_closed);
    }
    reset_case();
    usb_error = ESP_ERR_TIMEOUT;
    usb_quiesced = false;
    assert(request_power_off() == ESP_ERR_TIMEOUT);
    assert(!called(Call::Pmu) && called(Call::UsbOpen) && called(Call::SettingsOpen));
    reset_case();
    usb_error = ESP_ERR_NOT_FOUND;
    assert(request_power_off() == ESP_FAIL);
    assert(called(Call::Pmu) && called(Call::UsbOpen) && called(Call::SpeechOpen));
    reset_case();
    usb_error = ESP_ERR_TIMEOUT;
    assert(request_power_off() == ESP_FAIL && called(Call::Pmu));
    reset_case();
    pmu_error = ESP_OK;
    assert(request_power_off() == ESP_ERR_TIMEOUT && called(Call::Wait));
    assert(s_shutdown_error == ESP_ERR_TIMEOUT && called(Call::DisplayError));
    assert(std::find(calls.begin(), calls.end(), Call::Prepare) < std::find(calls.begin(), calls.end(), Call::Standby));
    assert(std::find(calls.begin(), calls.end(), Call::SettingsSeal) < std::find(calls.begin(), calls.end(), Call::Pmu));
    assert(std::find(calls.begin(), calls.end(), Call::UsbSeal) < std::find(calls.begin(), calls.end(), Call::Pmu));
    assert(std::find(calls.begin(), calls.end(), Call::EnduranceRequest) <
           std::find(calls.begin(), calls.end(), Call::SettingsSeal));
    assert(std::find(calls.begin(), calls.end(), Call::SettingsOpen) <
           std::find(calls.begin(), calls.end(), Call::EnduranceCancel));
    reset_case();
    endurance_error = ESP_ERR_INVALID_RESPONSE;
    assert(request_power_off() == ESP_FAIL && called(Call::Pmu));
    assert(called(Call::EnduranceRequest) && called(Call::EnduranceCancel));
    reset_case();
    settings_open_error = ESP_ERR_TIMEOUT;
    assert(request_power_off() == ESP_FAIL && called(Call::SettingsOpen));
    assert(!called(Call::EnduranceCancel));

    LUNASHELL::ButtonHold hold;
    assert(!hold.update(false, 0, true, 100, 3000000));
    assert(!hold.update(true, 1000, true, 3000999, 3000000));
    assert(hold.update(true, 1000, true, 3001000, 3000000));
    assert(!hold.update(true, 1000, true, 6001000, 3000000));
    assert(!hold.update(false, 0, true, 6002000, 3000000));
    assert(!hold.update(true, 6003000, false, 6004000, 3000000));
    assert(!hold.update(true, 6003000, true, 10000000, 3000000));
    assert(!hold.update(false, 0, true, 11000000, 3000000));
    assert(hold.update(true, 12000000, true, 15000000, 3000000));
    std::puts("production power owner: PASS (fake peripherals; physical behavior unverified)");
}
