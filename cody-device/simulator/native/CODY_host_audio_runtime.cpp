#include "CODY_host_audio_codec.hpp"
#include "CODY_host_onboarding_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "CODY_codex_audio.hpp"
#include "CODY_device_effects.hpp"
#include "CODY_settings.hpp"
#include "esp_timer.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstring>
#include <new>

#if defined(_WIN32)
#define CODY_HOST_AUDIO_EXPORT extern "C" __declspec(dllexport)
#else
#define CODY_HOST_AUDIO_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
std::mutex s_start_mutex;
bool s_start_attempted = false;
esp_err_t s_start_result = ESP_ERR_NOT_FINISHED;
// Keep existing interactive playback as the default for callers predating Studio.
// Automated capture may explicitly opt out before any audio route is opened.
uint32_t s_output_mode = 0;
constexpr std::size_t kProbeCapacity = 65536;
std::mutex s_probe_mutex;
std::array<int16_t, kProbeCapacity> s_probe_samples{};
CodyHostAudioProbe s_probe{1, sizeof(CodyHostAudioProbe), 0, 0, 0,
    UINT64_C(14695981039346656037), 0, 0, 0, 0};
std::atomic<bool> s_audition_active{false};
std::mutex s_audition_mutex;
CodyHostAudioAudition s_audition{};
CODY::DeviceEffectsModel *const s_audition_effects = new (std::nothrow) CODY::DeviceEffectsModel{};

struct AuditionCue { const char *key; CODY::CodexAudioCue codex; CODY::PetCue pet; };
constexpr AuditionCue kAuditionCues[]{
    {"codex:working", CODY::CodexAudioCue::Working, {}},
    {"codex:reading", CODY::CodexAudioCue::Reading, {}},
    {"codex:editing", CODY::CodexAudioCue::Editing, {}},
    {"codex:running", CODY::CodexAudioCue::Running, {}},
    {"codex:response-ready", CODY::CodexAudioCue::ResponseReady, {}},
    {"codex:paused", CODY::CodexAudioCue::Paused, {}},
    {"codex:interrupted", CODY::CodexAudioCue::Interrupted, {}},
    {"codex:session-ended", CODY::CodexAudioCue::SessionEnded, {}},
    {"codex:connection-lost", CODY::CodexAudioCue::ConnectionLost, {}},
    {"codex:connection-restored", CODY::CodexAudioCue::ConnectionRestored, {}},
    {"codex:question", CODY::CodexAudioCue::Question, {}},
    {"codex:answer-received", CODY::CodexAudioCue::AnswerReceived, {}},
    {"codex:checks-passed", CODY::CodexAudioCue::ChecksPassed, {}},
    {"codex:checks-failed", CODY::CodexAudioCue::ChecksFailed, {}},
    {"codex:approval-required", CODY::CodexAudioCue::ApprovalRequired, {}},
    {"codex:question-reminder", CODY::CodexAudioCue::QuestionReminder, {}},
    {"codex:bookmark-saved", CODY::CodexAudioCue::BookmarkSaved, {}},
    {"codex:questions-open", CODY::CodexAudioCue::QuestionsOpen, {}},
    {"codex:activity-open", CODY::CodexAudioCue::ActivityOpen, {}},
    {"pet:boot-ding", CODY::CodexAudioCue::None, CODY::PetCue::BootDing},
    {"pet:birth-rise", CODY::CodexAudioCue::None, CODY::PetCue::BirthRise},
    {"pet:birth-greeting", CODY::CodexAudioCue::None, CODY::PetCue::BirthGreeting},
    {"pet:tap", CODY::CodexAudioCue::None, CODY::PetCue::Tap},
    {"pet:jump", CODY::CodexAudioCue::None, CODY::PetCue::Jump},
    {"pet:surprise", CODY::CodexAudioCue::None, CODY::PetCue::Surprise},
    {"pet:first-boot-melody", CODY::CodexAudioCue::None, CODY::PetCue::FirstBootMelody},
};

// state: Original shared-worker admission or playback disposition.
// Exception: No exceptions; unknown internal states are explicit failures.
const char *audition_state(lunashell_audio_cue_state_t state)
{
    switch (state) {
    case LUNASHELL_CUE_IDLE: return "idle";
    case LUNASHELL_CUE_DISPATCHED: return "dispatched";
    case LUNASHELL_CUE_PLAYING: return "playing";
    case LUNASHELL_CUE_COMPLETED: return "completed";
    case LUNASHELL_CUE_DROPPED_BUSY: return "dropped-busy";
    case LUNASHELL_CUE_DROPPED_COOLDOWN: return "dropped-cooldown";
    case LUNASHELL_CUE_SUPPRESSED: return "suppressed";
    case LUNASHELL_CUE_EXPIRED: return "expired";
    case LUNASHELL_CUE_FAILED: return "failed";
    case LUNASHELL_CUE_UNAVAILABLE: return "unavailable";
    }
    return "failed";
}

// state: Typed Codex wrapper status, mapped without relying on enum ordinals.
// Exception: No exceptions; returns the matching semantic owner state.
const char *audition_state(CODY::CodexAudioState state)
{
    switch (state) {
    case CODY::CodexAudioState::Idle: return "idle";
    case CODY::CodexAudioState::Dispatched: return "dispatched";
    case CODY::CodexAudioState::Playing: return "playing";
    case CODY::CodexAudioState::Completed: return "completed";
    case CODY::CodexAudioState::DroppedBusy: return "dropped-busy";
    case CODY::CodexAudioState::DroppedCooldown: return "dropped-cooldown";
    case CODY::CodexAudioState::Suppressed: return "suppressed";
    case CODY::CodexAudioState::Expired: return "expired";
    case CODY::CodexAudioState::Failed: return "failed";
    case CODY::CodexAudioState::Unavailable: return "unavailable";
    }
    return "failed";
}

// The test sink observes real native mixer PCM without opening an audio device.
// It applies the same mute and linear codec-volume projection as the ALSA sink.
// Synchronous real-time backpressure keeps the firmware worker from running ahead.
esp_err_t test_sink(const int16_t *samples, std::size_t count, uint32_t rate,
    int volume, bool muted, void *)
{
    if (!samples || !count || count > 960 || rate < 8000 || rate > 48000 ||
        volume < 0 || volume > 100) return ESP_ERR_INVALID_ARG;
    {
        std::lock_guard<std::mutex> lock(s_probe_mutex);
        ++s_probe.blocks;
        s_probe.last_rate = rate;
        for (std::size_t i = 0; i < count; ++i) {
            const auto sample = static_cast<int16_t>(muted ? 0 :
                static_cast<int32_t>(samples[i]) * volume / 100);
            const auto magnitude = sample < 0 ? -static_cast<int32_t>(sample) : sample;
            if (static_cast<uint32_t>(magnitude) > s_probe.peak) s_probe.peak = magnitude;
            if (sample != 0) ++s_probe.nonzero_frames;
            const auto bits = static_cast<uint16_t>(sample);
            s_probe.effective_hash = (s_probe.effective_hash ^ (bits & 0xffU)) * UINT64_C(1099511628211);
            s_probe.effective_hash = (s_probe.effective_hash ^ (bits >> 8U)) * UINT64_C(1099511628211);
            if (s_probe.stored_frames < s_probe_samples.size())
                s_probe_samples[s_probe.stored_frames++] = sample;
            else s_probe.truncated = 1;
            ++s_probe.frames;
        }
    }
    std::this_thread::sleep_for(std::chrono::nanoseconds(
        (static_cast<uint64_t>(count) * UINT64_C(1000000000) + rate - 1) / rate));
    return ESP_OK;
}

} // namespace

// No parameters: only the dedicated audition process sets this gate before live time.
// Exception: No exceptions; ordinary display processing remains unchanged.
bool cody_host_audio_audition_active()
{
    return s_audition_active.load(std::memory_order_acquire);
}

// key: Explicit authored cue key; unknown keys are rejected before settings mutation.
// sounds: User sound policy, exactly zero or one.
// volume: Confirmed shared codec level, zero through 100 percent.
// output: Receives the actual admission status, including original owner errors.
// capacity: Exact output ABI size; validation failure leaves output untouched.
// Exception: Returns size/identity/state/settings/owner errors without fabricating completion.
CODY_HOST_AUDIO_EXPORT esp_err_t cody_host_audio_audition_begin(const char *key, uint32_t sounds,
    uint32_t volume, CodyHostAudioAudition *output, size_t capacity)
{
    if (!output || !key || sounds > 1 || volume > 100) return ESP_ERR_INVALID_ARG;
    if (capacity != sizeof(CodyHostAudioAudition)) return ESP_ERR_INVALID_SIZE;
    const AuditionCue *selected = nullptr;
    for (const auto &cue : kAuditionCues) if (std::strcmp(key, cue.key) == 0) selected = &cue;
    if (!selected) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_audition_mutex);
    if (s_audition_active.load(std::memory_order_acquire) ||
        cody_host_manual_time_us.load(std::memory_order_acquire) != 1000000) return ESP_ERR_INVALID_STATE;
    if (!s_audition_effects) return ESP_ERR_NO_MEM;
    auto error = CODY::persist_companion_preference(CODY::PreferenceId::PlaybackVolume, volume);
    if (error == ESP_OK) error = CODY::persist_companion_preference(CODY::PreferenceId::Sounds, sounds);
    if (error == ESP_OK) error = CODY::prepare_device_effects(*s_audition_effects);
    if (error == ESP_OK) error = CODY::set_codex_audio_suspended(false);
    if (error == ESP_OK) error = LUNASHELL_audio_set_personality_suspended(false);
    if (error != ESP_OK) return error;
    // Park admission first, then release the monotonic gate forward. The native
    // audio owner progresses naturally; no unrelated display event can enqueue sound.
    s_audition_active.store(true, std::memory_order_release);
    using clock = std::chrono::steady_clock;
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(clock::now().time_since_epoch()).count();
    cody_host_live_origin_steady_us.store(now, std::memory_order_release);
    cody_host_manual_time_us.store(0, std::memory_order_release);
    s_audition = {};
    if (selected->codex != CODY::CodexAudioCue::None) {
        CODY::CodexAudioStatus status{};
        error = CODY::notify_codex_audio(selected->codex, status);
        s_audition.cue_id = static_cast<uint32_t>(selected->codex);
        s_audition.sequence = status.request_sequence;
        s_audition.error = status.last_error;
        std::strcpy(s_audition.state, audition_state(status.state));
    } else {
        error = CODY::play_pet_cue(*s_audition_effects, selected->pet, nullptr);
        const auto &status = s_audition_effects->pet_audio_status;
        s_audition.cue_id = static_cast<uint32_t>(selected->pet);
        s_audition.sequence = status.request_sequence;
        s_audition.error = status.last_error;
        std::strcpy(s_audition.state, audition_state(status.state));
    }
    if (error != ESP_OK && s_audition.error == ESP_OK) s_audition.error = error;
    *output = s_audition;
    return error;
}

// output: Receives only this explicitly admitted cue's current owner status.
// capacity: Exact ABI capacity, rejected before any write.
// Exception: Preserves original query errors; no sequence means the original admission was terminal.
CODY_HOST_AUDIO_EXPORT esp_err_t cody_host_audio_audition_poll(CodyHostAudioAudition *output, size_t capacity)
{
    if (!output) return ESP_ERR_INVALID_ARG;
    if (capacity != sizeof(CodyHostAudioAudition)) return ESP_ERR_INVALID_SIZE;
    std::lock_guard<std::mutex> lock(s_audition_mutex);
    if (!s_audition_active.load(std::memory_order_acquire)) return ESP_ERR_INVALID_STATE;
    if (s_audition.sequence != 0) {
        lunashell_audio_cue_status_t status{};
        const auto error = LUNASHELL_audio_get_cue_status(s_audition.sequence, s_audition.cue_id, &status);
        if (error != ESP_OK) return error;
        s_audition.error = status.last_error;
        std::strcpy(s_audition.state, audition_state(status.state));
    }
    *output = s_audition;
    return ESP_OK;
}

// No parameters: exposes the checked capacity before Python supplies a native buffer.
// Exception: No exceptions.
CODY_HOST_AUDIO_EXPORT size_t cody_host_audio_audition_size()
{
    return sizeof(CodyHostAudioAudition);
}

// mode: 0 uses host output; 1 suppresses it; 2 selects the in-memory test sink.
// Exception: Rejects unknown modes or changes after the first startup attempt.
CODY_HOST_AUDIO_EXPORT esp_err_t cody_host_audio_configure(uint32_t mode)
{
    if (mode > 2) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_start_mutex);
    if (s_start_attempted) return ESP_ERR_INVALID_STATE;
    s_output_mode = mode;
    return ESP_OK;
}

// mode: Receives the explicit output policy (0 host output, 1 silent, 2 test sink).
// attempted: Receives whether backend initialization has been attempted.
// backend_result: Receives the original startup result, including NOT_FINISHED
// before startup and NOT_SUPPORTED when output was deliberately suppressed.
// Exception: Null output pointers are rejected without modifying any output.
CODY_HOST_AUDIO_EXPORT esp_err_t cody_host_audio_status(uint32_t *mode, uint32_t *attempted,
    int *backend_result)
{
    if (!mode || !attempted || !backend_result) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_start_mutex);
    *mode = s_output_mode;
    *attempted = s_start_attempted ? 1U : 0U;
    *backend_result = s_start_result;
    return ESP_OK;
}

// receipt: Exact-sized caller-owned test observation; not a claim of audible output.
// Exception: Rejects invalid output or a mode other than the explicit test sink.
CODY_HOST_AUDIO_EXPORT esp_err_t cody_host_audio_test_snapshot(CodyHostAudioProbe *receipt,
    std::size_t receipt_size)
{
    if (!receipt) return ESP_ERR_INVALID_ARG;
    if (receipt_size != sizeof(CodyHostAudioProbe)) return ESP_ERR_INVALID_SIZE;
    std::lock_guard<std::mutex> start_lock(s_start_mutex);
    if (s_output_mode != 2) return ESP_ERR_INVALID_STATE;
    std::lock_guard<std::mutex> probe_lock(s_probe_mutex);
    *receipt = s_probe;
    return ESP_OK;
}

// samples: Receives all retained effective samples; no partial output is published.
// capacity: Number of int16_t slots, not bytes.
// written: Receives the retained count on success.
// Exception: Rejects other modes, null output or insufficient capacity without writes.
CODY_HOST_AUDIO_EXPORT esp_err_t cody_host_audio_test_read(int16_t *samples, std::size_t capacity,
    std::size_t *written)
{
    if (!samples || !written) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> start_lock(s_start_mutex);
    if (s_output_mode != 2) return ESP_ERR_INVALID_STATE;
    std::lock_guard<std::mutex> probe_lock(s_probe_mutex);
    if (capacity < s_probe.stored_frames) return ESP_ERR_INVALID_SIZE;
    for (uint32_t i = 0; i < s_probe.stored_frames; ++i) samples[i] = s_probe_samples[i];
    *written = s_probe.stored_frames;
    return ESP_OK;
}

// receipt: Exact-sized observation of the firmware playback snapshot owner.
// Exception: Rejects other modes and preserves source errors without writing output.
CODY_HOST_AUDIO_EXPORT esp_err_t cody_host_audio_test_playback(CodyHostAudioPlaybackProbe *receipt,
    std::size_t receipt_size)
{
    if (!receipt) return ESP_ERR_INVALID_ARG;
    if (receipt_size != sizeof(CodyHostAudioPlaybackProbe)) return ESP_ERR_INVALID_SIZE;
    std::lock_guard<std::mutex> start_lock(s_start_mutex);
    if (s_output_mode != 2) return ESP_ERR_INVALID_STATE;
    LUNASHELL::PlaybackSnapshot playback{};
    const auto error = CODY::host_onboarding::read_playback(playback);
    if (error != ESP_OK) return error;
    uint32_t state = 0;
    switch (playback.state) {
    case LUNASHELL::PlaybackState::Unavailable: state = 0; break;
    case LUNASHELL::PlaybackState::Idle: state = 1; break;
    case LUNASHELL::PlaybackState::Playing: state = 2; break;
    case LUNASHELL::PlaybackState::Failed: state = 3; break;
    }
    const CodyHostAudioPlaybackProbe result{1, sizeof(CodyHostAudioPlaybackProbe), state,
        playback.cue_id, playback.last_error, playback.data_valid ? 1U : 0U,
        playback.data_stale ? 1U : 0U, playback.sample_rate_hz, playback.volume_percent,
        playback.last_attempt_at_us};
    *receipt = result;
    return ESP_OK;
}

// No parameters: registers the host stream then initializes the unmodified audio owner.
// Call before the production settings bootstrap, which alone may open the policy gate.
// Exception: Preserves the first backend or native owner error; no playback is requested.
CODY_HOST_AUDIO_EXPORT esp_err_t cody_host_audio_prepare()
{
    std::lock_guard<std::mutex> lock(s_start_mutex);
    if (s_start_attempted) return s_start_result;
    s_start_attempted = true;
    if (s_output_mode == 1) {
        // Do not open the host device from an isolated capture process.
        s_start_result = ESP_ERR_NOT_SUPPORTED;
        return s_start_result;
    }
    if (s_output_mode == 2) {
        s_start_result = cody_host_audio_register_sink(test_sink, nullptr);
        if (s_start_result == ESP_OK) s_start_result = LUNASHELL_audio_init();
        return s_start_result;
    }
#if defined(_WIN32)
    s_start_result = cody_host_audio_use_winmm();
#elif defined(__linux__)
    s_start_result = cody_host_audio_use_alsa();
#else
    s_start_result = ESP_ERR_NOT_SUPPORTED;
#endif
    if (s_start_result == ESP_OK) s_start_result = LUNASHELL_audio_init();
    return s_start_result;
}
