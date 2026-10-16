// Verbatim power-owner admission with synthetic state; no hardware or audio transport.
#include "LUNASHELL_voice_input.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

#define ESP_LOGE(...) ((void)0)

namespace LUNASHELL {
enum class DeviceOperatingState { Active, Standby };
enum class DeviceStateReason { PowerButton, BootButton, Touch, ImuMotion, WakeWord, CodexNotification };
struct VoiceSnapshot { VoiceInputObservation input{}; };
VoiceSnapshot fixture_voice{};
esp_err_t read_error = ESP_OK;
esp_err_t admission_error = ESP_OK;
unsigned admission_count = 0;

struct DeviceSnapshotService {
    // No parameters: deterministic owner projection, never sensor acquisition.
    static DeviceSnapshotService &instance() { static DeviceSnapshotService owner; return owner; }
    // voice: Receives the synthetic canonical fixture only after a successful read.
    esp_err_t readVoiceObservation(VoiceSnapshot &voice) { voice = fixture_voice; return read_error; }
};

// trigger: Must preserve the real completed standby-wake identity.
// Exception: Returns the injected real admission error.
esp_err_t requestVoiceInput(VoiceInputTrigger trigger)
{
    assert(trigger == VoiceInputTrigger::StandbyWake);
    ++admission_count;
    return admission_error;
}
}

LUNASHELL::DeviceOperatingState s_state = LUNASHELL::DeviceOperatingState::Standby;
bool s_shutdown_in_progress = false;
bool s_codex_offline_standby = false;
bool complete_wake = true;
esp_err_t transition_error = ESP_OK;

// standby: Same requested power target as production.
// reason: Original physical, speech or notification identity.
// Exception: Returns the injected transition error without hiding a completed optional-failure wake.
esp_err_t set_standby(bool standby, LUNASHELL::DeviceStateReason reason)
{
    (void)reason;
    if (standby) s_state = LUNASHELL::DeviceOperatingState::Standby;
    else if (complete_wake) s_state = LUNASHELL::DeviceOperatingState::Active;
    return transition_error;
}

#include "voice_wake_under_test.inc"

// No parameters: every case begins with a distinct genuine standby event.
void reset()
{
    s_state = LUNASHELL::DeviceOperatingState::Standby;
    s_shutdown_in_progress = false;
    s_codex_offline_standby = false;
    complete_wake = true;
    transition_error = LUNASHELL::read_error = LUNASHELL::admission_error = ESP_OK;
    LUNASHELL::fixture_voice = {};
    LUNASHELL::admission_count = 0;
}

// No parameters: execute the production wrapper against physical, internal and failure boundaries.
int main()
{
    using namespace LUNASHELL;
    for (auto reason : {DeviceStateReason::PowerButton, DeviceStateReason::BootButton,
                        DeviceStateReason::Touch, DeviceStateReason::ImuMotion}) {
        reset();
        assert(wake_from_user_input(false, reason) == ESP_OK && admission_count == 1);
        assert(wake_from_user_input(false, reason) == ESP_OK && admission_count == 1);
    }
    for (auto reason : {DeviceStateReason::WakeWord, DeviceStateReason::CodexNotification}) {
        reset();
        assert(wake_from_user_input(false, reason) == ESP_OK && admission_count == 0);
    }
    for (auto phase : {VoiceInputPhase::Preparing, VoiceInputPhase::StartCountdown, VoiceInputPhase::Listening,
                      VoiceInputPhase::SendCountdown, VoiceInputPhase::Sending, VoiceInputPhase::Cancelling,
                      VoiceInputPhase::Received}) {
        reset();
        fixture_voice.input.phase = phase;
        assert(wake_from_user_input(false, DeviceStateReason::Touch) == ESP_OK && admission_count == 0);
    }
    reset();
    s_codex_offline_standby = true;
    assert(wake_from_user_input(false, DeviceStateReason::Touch) == ESP_OK && admission_count == 0);
    assert(s_state == DeviceOperatingState::Active); // First offline wake contact only restores Cody.
    reset();
    s_shutdown_in_progress = true;
    assert(wake_from_user_input(false, DeviceStateReason::PowerButton) == ESP_OK && admission_count == 0);
    reset();
    complete_wake = false;
    transition_error = ESP_FAIL;
    assert(wake_from_user_input(false, DeviceStateReason::Touch) == ESP_FAIL && admission_count == 0);
    reset();
    read_error = ESP_ERR_TIMEOUT;
    assert(wake_from_user_input(false, DeviceStateReason::Touch) == ESP_ERR_TIMEOUT && admission_count == 0);
    reset();
    transition_error = ESP_ERR_TIMEOUT;
    admission_error = ESP_FAIL;
    assert(wake_from_user_input(false, DeviceStateReason::Touch) == ESP_ERR_TIMEOUT && admission_count == 1);
    reset();
    assert(wake_from_user_input(true, DeviceStateReason::PowerButton) == ESP_OK && admission_count == 0);
    std::puts("PASS completed physical wake, duplicate capture, shutdown and original errors");
}
