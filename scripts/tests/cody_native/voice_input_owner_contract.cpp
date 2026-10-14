// Verbatim voice-owner functions with fake PCM/publication boundaries, never hardware.
#include "LUNASHELL_voice_input.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <mutex>

#define ESP_LOGE(...) ((void)0)
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
constexpr size_t kSamples = 160000;
constexpr uint32_t kUtteranceSamples = 60U * 16000U;
constexpr uint32_t kSilenceSamples = 5U * 16000U;
constexpr int64_t kCountdownUs = 3000000;
std::mutex s_mutex;
int16_t storage[kSamples]{};
int16_t *s_ring = storage;
size_t s_read, s_write, s_count, s_inflight_samples, s_window_read, s_window_count, s_reserved, s_packet_size;
uint32_t s_sequence, s_ack_counter, s_silence_samples;
uint8_t s_session[16];
bool s_end_sent, s_begun, s_final, s_publication_pending;
int64_t s_last_ack_at, s_countdown_deadline_us, now_us = 1000000;
LUNASHELL::VoiceInputObservation s_observation, published;
esp_err_t injected_publication_error = ESP_OK;
esp_err_t injected_read_error = ESP_OK;

namespace LUNASHELL {
enum class VoiceState { Listening, Paused };
struct VoiceSnapshot {
    bool data_valid = true, data_stale = false;
    VoiceState state = VoiceState::Listening;
    esp_err_t last_error = ESP_OK;
};
VoiceSnapshot fixture_voice{};
struct DeviceSnapshotService {
    // No parameters: one deterministic canonical fake.
    static DeviceSnapshotService &instance() { static DeviceSnapshotService service; return service; }
    // voice: Current valid AFE observation, without sensor acquisition.
    esp_err_t readVoiceObservation(VoiceSnapshot &voice) { voice = fixture_voice; return injected_read_error; }
    // voice: Real owner transition, retained only after successful transport.
    esp_err_t reportVoiceInput(const VoiceInputObservation &voice) {
        if (injected_publication_error == ESP_OK) published = voice;
        return injected_publication_error;
    }
};
}
// No parameters: injectable monotonic clock.
int64_t esp_timer_get_time() { return now_us; }
// size: Requested owner allocation size.
// caps: Requested internal/external capability flags, unused on this host.
void *heap_caps_malloc(size_t size, unsigned caps) { (void)size; (void)caps; return storage; }
// destination: Fixture session identity destination.
// count: Exact requested number of public synthetic bytes.
void esp_fill_random(void *destination, size_t count) { (void)destination; (void)count; }
// No parameters: activity admission has no hardware effect in the fixture.
void LUNASHELL_power_manager_note_activity() {}

#include "voice_input_owner_under_test.inc"

// No parameters: a new session must prepare without capturing before its first complete screen.
void prepare()
{
    injected_publication_error = ESP_OK;
    injected_read_error = ESP_OK;
    LUNASHELL::fixture_voice = {};
    s_observation.phase = LUNASHELL::VoiceInputPhase::Idle;
    now_us += 1000000;
    assert(LUNASHELL::requestVoiceInput(LUNASHELL::VoiceInputTrigger::DoubleTap) == ESP_OK);
    assert(published.phase == LUNASHELL::VoiceInputPhase::Preparing);
    assert(published.capturing_started_at_us == 0 && published.countdown_seconds == 0);
}

// No parameters: deterministic successful frame retirement followed by three actual seconds.
void begin()
{
    prepare();
    bool accepted = false;
    assert(LUNASHELL::beginVoiceInputCountdown(published.started_at_us, accepted) == ESP_OK && accepted);
    now_us += 3000000;
    assert(advance_countdown_locked(now_us) == ESP_OK);
    assert(published.phase == LUNASHELL::VoiceInputPhase::Listening);
    assert(published.capturing_started_at_us == now_us && published.countdown_seconds == 0);
}

// No parameters: exercise real owner transitions plus the production cadence helper.
int main()
{
    using namespace LUNASHELL;
    prepare();
    const int64_t pending_identity = published.started_at_us;
    feedVoiceInput(storage, 16000, true, ESP_OK);
    assert(s_count == 0 && published.phase == VoiceInputPhase::Preparing);
    bool accepted = true;
    assert(beginVoiceInputCountdown(pending_identity - 1, accepted) == ESP_OK && !accepted);
    assert(beginVoiceInputCountdown(pending_identity, accepted) == ESP_OK && accepted);
    assert(published.phase == VoiceInputPhase::StartCountdown && published.countdown_seconds == 3);
    for (unsigned elapsed = 1; elapsed <= 2; ++elapsed) {
        now_us += 1000000;
        assert(advance_countdown_locked(now_us) == ESP_OK);
        assert(published.countdown_seconds == 3 - elapsed && published.captured_samples == 0);
        feedVoiceInput(storage, 16000, true, ESP_OK);
        assert(s_count == 0);
    }
    assert(cancelVoiceInput(pending_identity, accepted) == ESP_OK && accepted);
    now_us += 1000000;
    assert(advance_countdown_locked(now_us) == ESP_OK && published.phase == VoiceInputPhase::Cancelled);

    prepare();
    assert(beginVoiceInputCountdown(published.started_at_us, accepted) == ESP_OK && accepted);
    injected_read_error = ESP_FAIL;
    now_us += 3000000;
    assert(advance_countdown_locked(now_us) == ESP_FAIL);
    assert(published.phase == VoiceInputPhase::Failed && published.error == ESP_FAIL);

    prepare();
    assert(cancelVoiceInput(published.started_at_us, accepted) == ESP_OK && accepted);
    fixture_voice.state = VoiceState::Paused;
    fixture_voice.data_stale = true;
    assert(requestVoiceInput(VoiceInputTrigger::StandbyWake) == ESP_OK);
    assert(published.phase == VoiceInputPhase::Preparing && published.captured_samples == 0);
    assert(beginVoiceInputCountdown(published.started_at_us, accepted) == ESP_OK && accepted);
    now_us += 3000000;
    assert(advance_countdown_locked(now_us) == ESP_ERR_INVALID_STATE);
    assert(published.phase == VoiceInputPhase::Failed); // A stale pause never becomes actual Listening.

    begin();
    now_us += 8000000; // A scheduling gap is not eight seconds of acquired silence.
    feedVoiceInput(storage, 79999, false, ESP_OK);
    assert(published.phase == VoiceInputPhase::Listening);
    feedVoiceInput(storage, 1, false, ESP_OK); // Initial silence also reaches the exact five-second bound.
    assert(published.phase == VoiceInputPhase::SendCountdown && published.countdown_seconds == 3);
    now_us += 2000000;
    assert(advance_countdown_locked(now_us) == ESP_OK && published.countdown_seconds == 1);
    assert(cancelVoiceInput(published.started_at_us, accepted) == ESP_OK && accepted);
    now_us += 1000000;
    assert(advance_countdown_locked(now_us) == ESP_OK && published.phase == VoiceInputPhase::Cancelled);

    begin();
    feedVoiceInput(storage, 1, true, ESP_OK);
    now_us += 3000000; // A scheduling gap contributes no acquired silent samples.
    feedVoiceInput(storage, 1, false, ESP_OK);
    assert(published.phase == VoiceInputPhase::Listening);
    feedVoiceInput(storage, 79998, false, ESP_OK);
    assert(published.phase == VoiceInputPhase::Listening); // One real sample short of five seconds.
    feedVoiceInput(storage, 1, false, ESP_OK);
    assert(published.phase == VoiceInputPhase::SendCountdown);
    now_us += 2999999;
    assert(advance_countdown_locked(now_us) == ESP_OK && published.countdown_seconds == 1);
    ++now_us;
    assert(advance_countdown_locked(now_us) == ESP_OK);
    assert(published.phase == VoiceInputPhase::Sending);
    begin();
    assert(s_silence_samples == 0);
    feedVoiceInput(storage, 1, true, ESP_OK);
    feedVoiceInput(storage, 32000, false, ESP_OK);
    feedVoiceInput(storage, 1, true, ESP_OK);
    feedVoiceInput(storage, 1, false, ESP_OK);
    assert(published.phase == VoiceInputPhase::Listening && s_silence_samples == 1);
    const int64_t identity = published.started_at_us;
    accepted = true;
    assert(finishVoiceInput(identity - 1, accepted) == ESP_OK && !accepted);
    assert(finishVoiceInput(identity, accepted) == ESP_OK && accepted && published.phase == VoiceInputPhase::SendCountdown);
    assert(finishVoiceInput(identity, accepted) == ESP_OK && !accepted);
    s_begun = true;
    s_sequence = 12;
    s_packet_size = 100;
    s_window_count = 4;
    s_reserved = 40;
    assert(cancelVoiceInput(identity, accepted) == ESP_OK && accepted);
    assert(published.phase == VoiceInputPhase::Cancelling && s_sequence == 13 &&
           s_packet_size == 0 && s_window_count == 0 && s_reserved == 0 && s_count == 0);
    assert(cancelVoiceInput(identity, accepted) == ESP_OK && !accepted);
    begin();
    feedVoiceInput(storage, 1, true, ESP_OK);
    s_end_sent = true;
    s_observation.phase = VoiceInputPhase::Sending;
    assert(cancelVoiceInput(s_observation.started_at_us, accepted) == ESP_OK && !accepted);
    assert(cancelVoiceInput() == ESP_ERR_INVALID_STATE);
    begin();
    assert(finishVoiceInput(s_observation.started_at_us, accepted) == ESP_OK && !accepted);
    feedVoiceInput(storage, 1, true, ESP_OK);
    injected_publication_error = ESP_ERR_TIMEOUT;
    assert(finishVoiceInput(s_observation.started_at_us, accepted) == ESP_ERR_TIMEOUT && !accepted);
    assert(s_observation.phase == VoiceInputPhase::Failed && s_publication_pending && s_observation.countdown_seconds == 0);
    begin();
    feedVoiceInput(storage, 1, true, ESP_OK);
    injected_publication_error = ESP_FAIL;
    assert(cancelVoiceInput(s_observation.started_at_us, accepted) == ESP_FAIL && !accepted);
    begin();
    feedVoiceInput(storage, 1, false, ESP_FAIL);
    assert(published.phase == VoiceInputPhase::Failed); // Failed acquisition is never silence.
    prepare();
    assert(pauseVoiceInput() == ESP_OK && published.phase == VoiceInputPhase::Cancelled);
    for (int64_t offset = 0; offset < 100000; offset += 1000) {
        int64_t time = offset;
        unsigned polls = 0;
        while (time <= voice_poll::kAckTimeoutUs) {
            const bool fast = voice_poll::disconnected_needs_fast_poll(time, 0, 0);
            time += (fast ? voice_poll::kActiveMs : voice_poll::kIdleMs) * 1000;
            ++polls;
        }
        assert(time - voice_poll::kAckTimeoutUs <= voice_poll::kActiveMs * 1000 && polls < 130);
    }
    assert(voice_poll::disconnected_needs_fast_poll(89900000, 0, 89900000));
    assert(!voice_poll::disconnected_needs_fast_poll(89899999, 0, 89899999));
    std::puts("PASS real voice owner countdowns, gestures, five-second acquired silence and disconnected cadence");
}
