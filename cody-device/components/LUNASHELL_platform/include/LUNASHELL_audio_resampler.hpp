#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_log.h"

namespace LUNASHELL {

// The owner retains this state across producer blocks. Only left and right are
// copied; all unconsumed input remains owned by the producer. A rational phase
// preserves the sample clock across both producer and consumer block boundaries.
struct AudioPcmResampler {
    uint32_t source_rate_hz = 0;
    uint32_t transport_rate_hz = 0;
    uint32_t phase = 0;
    int16_t left = 0;
    int16_t right = 0;
    bool has_left = false;
    bool has_right = false;
    bool initialized = false;
    bool end_seen = false;
    bool finished = false;
};

// state: Caller-owned inactive or finished stream state, reset on successful initialization.
// source_rate_hz: Input PCM sample rate from 8 to 48 kHz.
// transport_rate_hz: Output PCM sample rate from 8 to 48 kHz.
// Exception: Returns invalid argument or invalid state without changing the stream.
inline esp_err_t initialize_audio_resampler(AudioPcmResampler &state, uint32_t source_rate_hz,
    uint32_t transport_rate_hz)
{
    if (state.initialized && !state.finished) {
        ESP_LOGE("LUNASHELL_resampler", "Cannot replace an unfinished PCM stream");
        return ESP_ERR_INVALID_STATE;
    }
    if (source_rate_hz < 8000 || source_rate_hz > 48000 ||
        transport_rate_hz < 8000 || transport_rate_hz > 48000) {
        ESP_LOGE("LUNASHELL_resampler", "Invalid sample rates source=%lu transport=%lu",
            static_cast<unsigned long>(source_rate_hz), static_cast<unsigned long>(transport_rate_hz));
        return ESP_ERR_INVALID_ARG;
    }
    state = {};
    state.source_rate_hz = source_rate_hz;
    state.transport_rate_hz = transport_rate_hz;
    state.initialized = true;
    return ESP_OK;
}

// state: Initialized stream state, retained and serialized by the output owner.
// input: Borrowed mono PCM; null is allowed only when input_frames is zero.
// input_frames: Available input samples; any unconsumed suffix remains with the caller.
// final_input: This input ends the stream; keep true when resubmitting its unconsumed suffix.
// consumed: Receives the exact number of input samples copied into the stream state.
// output: Caller-owned mono PCM destination, disjoint from the input buffer.
// output_capacity: Number of available output slots, from one through 512.
// produced: Receives the exact number of valid output samples; no silence is fabricated.
// Exception: Returns invalid argument or invalid state without modifying the stream or output.
// A final call may produce no samples while setting finished; drain until finished is true.
// Linear interpolation retains phase and the boundary samples, but is not an anti-alias filter.
inline esp_err_t resample_audio_pcm(AudioPcmResampler &state, const int16_t *input,
    size_t input_frames, bool final_input, size_t *consumed, int16_t *output,
    size_t output_capacity, size_t *produced)
{
    if (consumed == nullptr || produced == nullptr || output == nullptr ||
        (input_frames != 0 && input == nullptr) || output_capacity == 0 || output_capacity > 512) {
        ESP_LOGE("LUNASHELL_resampler", "PCM block rejected: input=%u capacity=%u",
            static_cast<unsigned>(input_frames), static_cast<unsigned>(output_capacity));
        return ESP_ERR_INVALID_ARG;
    }
    if (!state.initialized || state.finished || (state.end_seen && input_frames != 0)) {
        ESP_LOGE("LUNASHELL_resampler", "PCM stream is not accepting input");
        return ESP_ERR_INVALID_STATE;
    }
    size_t input_index = 0;
    size_t output_index = 0;
    while (output_index < output_capacity) {
        if (!state.has_left) {
            if (input_index < input_frames) {
                state.left = input[input_index++];
                state.has_left = true;
            } else {
                if (final_input || state.end_seen) {
                    state.end_seen = true;
                    state.finished = true;
                }
                break;
            }
        }
        while (state.phase >= state.transport_rate_hz) {
            if (!state.has_right && input_index < input_frames) {
                state.right = input[input_index++];
                state.has_right = true;
            }
            if (!state.has_right) {
                if (final_input || state.end_seen) {
                    state.end_seen = true;
                    state.finished = true;
                }
                break;
            }
            state.left = state.right;
            state.has_right = false;
            state.phase -= state.transport_rate_hz;
        }
        if (state.finished || state.phase >= state.transport_rate_hz) break;
        if (!state.has_right && input_index < input_frames) {
            state.right = input[input_index++];
            state.has_right = true;
        }
        if (!state.has_right && !(final_input || state.end_seen)) break;
        // At EOF, holding the last actual sample for its fractional interval
        // completes the duration without adding an artificial silent sample.
        const int16_t right = state.has_right ? state.right : state.left;
        const int64_t weighted = static_cast<int64_t>(state.left) * (state.transport_rate_hz - state.phase) +
            static_cast<int64_t>(right) * state.phase;
        output[output_index++] = static_cast<int16_t>(weighted / state.transport_rate_hz);
        state.phase += state.source_rate_hz;
    }
    if (final_input && input_index == input_frames) state.end_seen = true;
    *consumed = input_index;
    *produced = output_index;
    return ESP_OK;
}

} // namespace LUNASHELL
