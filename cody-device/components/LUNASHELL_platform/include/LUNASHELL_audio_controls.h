#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// The pinned esp_codec_dev 1.5.11 getters expose driver state, not hardware
// registers; that library can discard errors from its codec-setting callbacks.
// Success below therefore establishes driver admission only, not physical
// application. Consumers must preserve this distinction in their snapshots.

// Cody startup opts in before publishing any cue; Luna never arms this gate.
// The gate remains closed until its settings owner reports successful output
// reconciliation and publication. It does not change Sounds or microphone state.
void LUNASHELL_audio_begin_cody_output_policy(void);

// error: Original output reconciliation/publication error, or ESP_OK after success.
// Once opened, the startup-only gate cannot close again during the same boot.
void LUNASHELL_audio_update_cody_output_policy(esp_err_t error);

// percent: Requested codec output volume, 0 through 100 inclusive.
// applied: Receives the codec driver's reported selection on success; not a hardware register read.
// Exception: Returns invalid input, unavailable/busy route or original codec failure.
// Blocking owner operation; never call from the display or audio callback.
esp_err_t LUNASHELL_audio_set_user_output_volume(uint8_t percent, uint8_t *applied);

// applied: Receives the current selection reported by the codec driver, not a register read.
// Exception: Returns unavailable/busy route, codec read or invalid readback failure.
// Blocking owner operation; consumers obtain the published result from snapshots.
esp_err_t LUNASHELL_audio_read_user_output_volume(uint8_t *applied);

// gain_db: Requested shared ES7210 gain, 0 through 30 dB in 3-dB steps.
// applied: Receives the codec driver's reported selection on success; not a hardware register read.
// Exception: Returns invalid input, busy capture/route, unavailable route or codec failure.
// The AFE's current RX read is serialized; busy requests never stop or reconfigure capture.
esp_err_t LUNASHELL_audio_set_user_input_gain(uint8_t gain_db, uint8_t *applied);

// applied: Receives the driver-reported shared microphone gain, not a register read.
// Exception: Returns busy capture/route, unavailable route or invalid readback.
// Blocking owner operation; consumers obtain the published result from snapshots.
esp_err_t LUNASHELL_audio_read_user_input_gain(uint8_t *applied);

// samples: Caller-owned destination for measured interleaved microphone PCM16.
// frame_count: Requested sample frames per channel.
// channel_count: Configured microphone topology, one or two.
// sample_rate_hz: Requested diagnostic transport rate.
// Exception: Returns original capture or ownership error; gain is the driver-reported user policy.
esp_err_t LUNASHELL_audio_capture_user_levels_pcm16(int16_t *samples, size_t frame_count,
    uint8_t channel_count, uint32_t sample_rate_hz);

#ifdef __cplusplus
}
#endif
