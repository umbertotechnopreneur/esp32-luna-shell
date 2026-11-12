#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace LUNASHELL {

constexpr size_t kAudioStreamBlockFrames = 320;
constexpr size_t kAudioStreamBlockCount = 4;

struct AudioStreamBlock {
    std::array<int16_t, kAudioStreamBlockFrames> pcm{};
    size_t frames = 0;
};

// One producer copies source PCM into bounded storage; one output worker copies
// it out and acknowledges the in-flight block. The owner notifies its worker
// after successful enqueue, EOF or cancellation. No pipe call holds a codec
// lock or a caller's PCM beyond its return, and pipe waits are bounded.
struct AudioStreamPipe {
    StaticSemaphore_t lock_storage{};
    StaticSemaphore_t changed_storage{};
    SemaphoreHandle_t lock = nullptr;
    SemaphoreHandle_t changed = nullptr;
    std::array<AudioStreamBlock, kAudioStreamBlockCount> blocks{};
    size_t read = 0;
    size_t write = 0;
    size_t count = 0;
    uint32_t generation = 0;
    uint32_t request_id = 0;
    esp_err_t error = ESP_OK;
    bool active = false;
    bool input_ended = false;
    bool cancelled = false;
    bool in_flight = false;
};

struct AudioStreamStatus {
    uint32_t generation = 0;
    uint32_t request_id = 0;
    size_t queued_blocks = 0;
    esp_err_t error = ESP_OK;
    bool active = false;
    bool input_ended = false;
    bool cancelled = false;
    bool in_flight = false;
};

// pipe: Static storage, initialized once by its lifecycle owner before other tasks use it.
// Exception: Returns invalid state or semaphore creation failure; no audio I/O occurs.
inline esp_err_t initialize_audio_stream(AudioStreamPipe &pipe)
{
    if (pipe.lock != nullptr || pipe.changed != nullptr) return ESP_ERR_INVALID_STATE;
    pipe.lock = xSemaphoreCreateMutexStatic(&pipe.lock_storage);
    pipe.changed = xSemaphoreCreateBinaryStatic(&pipe.changed_storage);
    if (pipe.lock == nullptr || pipe.changed == nullptr) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

// pipe: Initialized stream, with no current producer or output-worker block.
// request_id: Nonzero identity owned by the producer.
// generation: Receives a never-reused session identity for this boot.
// Exception: Returns invalid arguments, busy state or generation exhaustion.
inline esp_err_t begin_audio_stream(AudioStreamPipe &pipe, uint32_t request_id, uint32_t *generation)
{
    if (pipe.lock == nullptr || request_id == 0 || generation == nullptr) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(pipe.lock, 0) != pdTRUE) return ESP_ERR_NOT_FINISHED;
    esp_err_t result = ESP_OK;
    if (pipe.active || pipe.in_flight) result = ESP_ERR_INVALID_STATE;
    else if (pipe.generation == std::numeric_limits<uint32_t>::max()) result = ESP_ERR_INVALID_STATE;
    else {
        ++pipe.generation;
        pipe.request_id = request_id;
        pipe.read = pipe.write = pipe.count = 0;
        pipe.error = ESP_OK;
        pipe.active = true;
        pipe.input_ended = pipe.cancelled = pipe.in_flight = false;
        *generation = pipe.generation;
    }
    xSemaphoreGive(pipe.lock);
    return result;
}

// pipe: Initialized stream with a single producer and consumer.
// generation: Exact session identity returned by begin_audio_stream.
// status: Receives a coherent state copy without accessing the codec.
// Exception: Returns invalid argument, lock contention or expired session identity.
inline esp_err_t observe_audio_stream(AudioStreamPipe &pipe, uint32_t generation, AudioStreamStatus *status)
{
    if (pipe.lock == nullptr || generation == 0 || status == nullptr) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(pipe.lock, 0) != pdTRUE) return ESP_ERR_NOT_FINISHED;
    const esp_err_t result = pipe.generation == generation ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) {
        *status = {pipe.generation, pipe.request_id, pipe.count, pipe.error,
            pipe.active, pipe.input_ended, pipe.cancelled, pipe.in_flight};
    }
    xSemaphoreGive(pipe.lock);
    return result;
}

// pipe: Initialized stream owned by a single producer.
// generation: Exact active session identity.
// samples: Borrowed PCM copied into pipe-owned storage before this function returns.
// frames: Number of PCM16 mono frames, one through kAudioStreamBlockFrames.
// timeout_ms: Maximum wait for queue space; zero performs one nonblocking attempt.
// Exception: Returns the original cancellation cause, timeout or invalid state/arguments.
inline esp_err_t enqueue_audio_stream(AudioStreamPipe &pipe, uint32_t generation,
    const int16_t *samples, size_t frames, uint32_t timeout_ms)
{
    if (pipe.lock == nullptr || pipe.changed == nullptr || generation == 0 || samples == nullptr ||
        frames == 0 || frames > kAudioStreamBlockFrames) return ESP_ERR_INVALID_ARG;
    const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
    for (;;) {
        if (xSemaphoreTake(pipe.lock, 0) == pdTRUE) {
            esp_err_t result = ESP_ERR_NOT_FINISHED;
            bool done = true;
            if (pipe.generation != generation) result = ESP_ERR_INVALID_STATE;
            else if (pipe.cancelled) result = pipe.error;
            else if (!pipe.active) result = ESP_ERR_INVALID_STATE;
            else if (pipe.input_ended) result = ESP_ERR_INVALID_STATE;
            else if (pipe.count < kAudioStreamBlockCount) {
                auto &block = pipe.blocks[pipe.write];
                std::memcpy(block.pcm.data(), samples, frames * sizeof(int16_t));
                block.frames = frames;
                pipe.write = (pipe.write + 1U) % kAudioStreamBlockCount;
                ++pipe.count;
                result = ESP_OK;
            } else done = false;
            xSemaphoreGive(pipe.lock);
            if (done) return result;
        }
        if (timeout_ms == 0 || esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
        // Periodic wake also supports cancellation and more than one waiting observer.
        xSemaphoreTake(pipe.changed, pdMS_TO_TICKS(10) + 1U);
    }
}

// pipe: Initialized stream; called only by its output worker.
// generation: Exact active session identity.
// block: Receives a private copy, valid independently of producer reuse.
// Exception: Returns NOT_FINISHED if no block/lock is ready, or the original cancellation cause.
// The worker must complete this block before another dequeue or terminal acknowledgement.
inline esp_err_t dequeue_audio_stream(AudioStreamPipe &pipe, uint32_t generation, AudioStreamBlock &block)
{
    if (pipe.lock == nullptr || generation == 0) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(pipe.lock, 0) != pdTRUE) return ESP_ERR_NOT_FINISHED;
    esp_err_t result = ESP_OK;
    if (pipe.generation != generation || !pipe.active || pipe.in_flight) result = ESP_ERR_INVALID_STATE;
    else if (pipe.cancelled) result = pipe.error;
    else if (pipe.count == 0) result = ESP_ERR_NOT_FINISHED;
    else {
        block = pipe.blocks[pipe.read];
        pipe.read = (pipe.read + 1U) % kAudioStreamBlockCount;
        --pipe.count;
        pipe.in_flight = true;
    }
    xSemaphoreGive(pipe.lock);
    if (result == ESP_OK) xSemaphoreGive(pipe.changed);
    return result;
}

// pipe: Initialized stream whose output worker owns one copied block.
// generation: Exact session identity associated with that block.
// cause: Original block rendering/transport error, or ESP_OK.
// Exception: Returns invalid state or lock contention; the original cause is retained in stream status.
inline esp_err_t complete_audio_stream_block(AudioStreamPipe &pipe, uint32_t generation, esp_err_t cause)
{
    if (pipe.lock == nullptr || generation == 0) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(pipe.lock, 0) != pdTRUE) return ESP_ERR_NOT_FINISHED;
    esp_err_t result = ESP_OK;
    if (pipe.generation != generation || !pipe.active || !pipe.in_flight) result = ESP_ERR_INVALID_STATE;
    else {
        pipe.in_flight = false;
        if (cause != ESP_OK && !pipe.cancelled) {
            pipe.cancelled = true;
            pipe.error = cause;
            pipe.read = pipe.write = pipe.count = 0;
        }
        result = ESP_OK;
    }
    xSemaphoreGive(pipe.lock);
    xSemaphoreGive(pipe.changed);
    return result;
}

// pipe: Initialized stream owned by its single producer.
// generation: Exact active session identity.
// Exception: Returns invalid state or lock contention; queued PCM remains owned by the pipe.
inline esp_err_t end_audio_stream_input(AudioStreamPipe &pipe, uint32_t generation)
{
    if (pipe.lock == nullptr || generation == 0) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(pipe.lock, 0) != pdTRUE) return ESP_ERR_NOT_FINISHED;
    esp_err_t result = ESP_OK;
    if (pipe.generation != generation) result = ESP_ERR_INVALID_STATE;
    else if (pipe.cancelled) result = pipe.error;
    else if (!pipe.active) result = ESP_ERR_INVALID_STATE;
    else if (pipe.input_ended) result = ESP_OK;
    else pipe.input_ended = true;
    xSemaphoreGive(pipe.lock);
    xSemaphoreGive(pipe.changed);
    return result;
}

// pipe: Initialized active stream, cancellable by its lifecycle owner.
// generation: Exact session identity; a later stream is never affected.
// cause: Actual non-success cancellation or operation failure.
// Exception: Returns invalid arguments, stale identity or lock contention.
// Queued blocks are dropped; an in-flight copied block remains owned by the worker.
inline esp_err_t cancel_audio_stream(AudioStreamPipe &pipe, uint32_t generation, esp_err_t cause)
{
    if (pipe.lock == nullptr || generation == 0 || cause == ESP_OK) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(pipe.lock, 0) != pdTRUE) return ESP_ERR_NOT_FINISHED;
    esp_err_t result = ESP_OK;
    if (pipe.generation != generation) result = ESP_ERR_INVALID_STATE;
    else if (pipe.cancelled) result = ESP_OK;
    else if (!pipe.active) result = ESP_ERR_INVALID_STATE;
    else {
        pipe.cancelled = true;
        pipe.error = cause;
        pipe.read = pipe.write = pipe.count = 0;
    }
    xSemaphoreGive(pipe.lock);
    xSemaphoreGive(pipe.changed);
    return result;
}

// pipe: Stream whose output worker has stopped accessing any source and retired the DMA tail.
// generation: Exact active session identity.
// cause: Final transport error, or ESP_OK; a prior cancellation cause remains authoritative.
// Exception: Rejects premature acknowledgement, stale identity or lock contention.
inline esp_err_t acknowledge_audio_stream_terminal(AudioStreamPipe &pipe, uint32_t generation, esp_err_t cause)
{
    if (pipe.lock == nullptr || generation == 0) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(pipe.lock, 0) != pdTRUE) return ESP_ERR_NOT_FINISHED;
    esp_err_t result = ESP_OK;
    if (pipe.generation != generation) result = ESP_ERR_INVALID_STATE;
    else if (!pipe.active) result = ESP_OK;
    else if (pipe.in_flight || (!pipe.cancelled && (!pipe.input_ended || pipe.count != 0)))
        result = ESP_ERR_INVALID_STATE;
    else {
        if (pipe.error == ESP_OK) pipe.error = cause;
        pipe.active = false;
    }
    xSemaphoreGive(pipe.lock);
    xSemaphoreGive(pipe.changed);
    return result;
}

// pipe: Initialized stream whose worker receives EOF/cancel notification from its owner.
// generation: Exact session identity.
// timeout_ms: Maximum wait for the worker's terminal and DMA-retirement acknowledgement.
// Exception: Returns timeout, stale identity or the original terminal/cancellation error.
// This must never be called while holding a codec, mixer or pipe lock.
inline esp_err_t wait_audio_stream_terminal(AudioStreamPipe &pipe, uint32_t generation, uint32_t timeout_ms)
{
    if (pipe.lock == nullptr || pipe.changed == nullptr || generation == 0) return ESP_ERR_INVALID_ARG;
    const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
    for (;;) {
        if (xSemaphoreTake(pipe.lock, 0) == pdTRUE) {
            const bool stale = pipe.generation != generation;
            const bool finished = !pipe.active;
            const esp_err_t result = pipe.error;
            xSemaphoreGive(pipe.lock);
            if (stale) return ESP_ERR_INVALID_STATE;
            if (finished) return result;
        }
        if (timeout_ms == 0 || esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
        xSemaphoreTake(pipe.changed, pdMS_TO_TICKS(10) + 1U);
    }
}

} // namespace LUNASHELL
