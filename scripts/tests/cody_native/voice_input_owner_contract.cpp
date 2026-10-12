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
std::mutex s_mutex;
int16_t storage[kSamples]{};
int16_t *s_ring = storage;
size_t s_read, s_write, s_count, s_inflight_samples, s_window_read, s_window_count, s_reserved, s_packet_size;
uint32_t s_sequence, s_ack_counter, s_silence_samples;
uint8_t s_session[16];
bool s_end_sent, s_begun, s_final, s_heard_speech, s_publication_pending;
int64_t s_last_ack_at, now_us = 1000000;
LUNASHELL::VoiceInputObservation s_observation, published;
esp_err_t injected_publication_error = ESP_OK;

namespace LUNASHELL {
enum class VoiceState { Listening };
struct VoiceSnapshot { bool data_valid = true, data_stale = false; VoiceState state = VoiceState::Listening; };
struct DeviceSnapshotService {
    // No parameters: one deterministic canonical fake.
    static DeviceSnapshotService &instance() { static DeviceSnapshotService service; return service; }
    // voice: Current valid AFE observation, without sensor acquisition.
    esp_err_t readVoiceObservation(VoiceSnapshot &voice) { voice = {}; return ESP_OK; }
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

// No parameters: use the real start path to reset every per-session endpoint guard.
void begin()
{
    injected_publication_error = ESP_OK;
    s_observation.phase = LUNASHELL::VoiceInputPhase::Idle;
    now_us += 1000000;
    assert(LUNASHELL::requestVoiceInput(LUNASHELL::VoiceInputTrigger::DoubleTap) == ESP_OK);
}

// No parameters: exercise real owner transitions plus the production cadence helper.
int main()
{
    using namespace LUNASHELL;
    begin();
    feedVoiceInput(storage, 1, true, ESP_OK);
    now_us += 3000000; // A scheduling gap contributes no acquired silent samples.
    feedVoiceInput(storage, 1, false, ESP_OK);
    assert(published.phase == VoiceInputPhase::Listening);
    feedVoiceInput(storage, 31999, false, ESP_OK);
    assert(published.phase == VoiceInputPhase::Listening); // Exactly two seconds.
    feedVoiceInput(storage, 1, false, ESP_OK);
    assert(published.phase == VoiceInputPhase::Sending);
    begin();
    assert(s_silence_samples == 0 && !s_heard_speech);
    feedVoiceInput(storage, 1, true, ESP_OK);
    feedVoiceInput(storage, 32000, false, ESP_OK);
    feedVoiceInput(storage, 1, true, ESP_OK);
    feedVoiceInput(storage, 1, false, ESP_OK);
    assert(published.phase == VoiceInputPhase::Listening && s_silence_samples == 1);
    const int64_t identity = published.started_at_us;
    bool accepted = true;
    assert(finishVoiceInput(identity - 1, accepted) == ESP_OK && !accepted);
    assert(finishVoiceInput(identity, accepted) == ESP_OK && accepted && published.phase == VoiceInputPhase::Sending);
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
    assert(s_observation.phase == VoiceInputPhase::Failed && s_publication_pending);
    begin();
    feedVoiceInput(storage, 1, true, ESP_OK);
    injected_publication_error = ESP_FAIL;
    assert(cancelVoiceInput(s_observation.started_at_us, accepted) == ESP_FAIL && !accepted);
    begin();
    feedVoiceInput(storage, 1, false, ESP_FAIL);
    assert(published.phase == VoiceInputPhase::Failed); // Failed acquisition is never silence.
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
    std::puts("PASS real voice owner gestures, acquired silence and disconnected cadence");
}
