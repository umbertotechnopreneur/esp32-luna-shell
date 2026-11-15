#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

// A sink must synchronously consume each PCM block before returning, apply the
// supplied volume and mute, and provide bounded backpressure. It owns no buffer.
using CodyHostPcmSink = esp_err_t (*)(const int16_t *samples, std::size_t count,
    uint32_t rate, int volume, bool muted, void *context);

// Bounded observation of the explicitly selected test sink, not audible-output evidence.
struct CodyHostAudioProbe {
    uint32_t abi;
    uint32_t size;
    uint64_t blocks;
    uint64_t frames;
    uint64_t nonzero_frames;
    uint64_t effective_hash;
    uint32_t last_rate;
    uint32_t peak;
    uint32_t stored_frames;
    uint32_t truncated;
};

// Canonical playback owner state: 0 unavailable, 1 idle, 2 playing, 3 failed.
struct CodyHostAudioPlaybackProbe {
    uint32_t abi;
    uint32_t size;
    uint32_t state;
    uint32_t cue_id;
    int32_t last_error;
    uint32_t data_valid;
    uint32_t data_stale;
    uint32_t sample_rate;
    uint32_t volume;
    int64_t observed_at_us;
};

// Semantic audition outcome; numeric firmware enums never cross this boundary.
struct CodyHostAudioAudition {
    uint32_t abi = 1;
    uint32_t size = sizeof(CodyHostAudioAudition);
    uint32_t cue_id = 0;
    uint32_t sequence = 0;
    int32_t error = 0;
    char state[24]{};
};

// No parameters: the isolated audition child keeps its display owner parked.
// Exception: No exceptions; ordinary native windows and boot replay return false.
bool cody_host_audio_audition_active();

// sink: Explicit host output owner; no callback means the audio route is unavailable.
// context: Borrowed host state retained until route deinitialization.
// Registration must precede native settings startup; it arms the real Cody policy
// and sets Sounds off until that settings owner applies a validated observation.
// Exception: Rejects null and registration after the native audio owner acquires the route.
esp_err_t cody_host_audio_register_sink(CodyHostPcmSink sink, void *context);

// No parameters: opts in to the Windows WinMM sink if a playback device exists.
// Exception: Returns the original registration/backend error; Linux is unsupported here.
esp_err_t cody_host_audio_use_winmm();

// No parameters: opts in to the Linux ALSA sink when its library and device exist.
// Exception: Returns NOT_SUPPORTED when ALSA is absent, or the original host error.
esp_err_t cody_host_audio_use_alsa();
