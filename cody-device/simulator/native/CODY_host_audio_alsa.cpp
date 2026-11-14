#include "CODY_host_audio_codec.hpp"
#include "esp_log.h"

#if defined(__linux__)
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <dlfcn.h>
#include <mutex>
#include <new>
#include <thread>

namespace {
constexpr const char *kTag = "CodyHostALSA";
// Public alsa-lib PCM enum values from pcm.h ABI; no private driver API is used.
// https://github.com/alsa-project/alsa-lib/blob/master/include/pcm.h
constexpr int kPlayback = 0;
constexpr int kNonblocking = 1;
constexpr int kS16LittleEndian = 2;
constexpr int kInterleaved = 3;
struct AlsaOutput {
    std::mutex mutex;
    void *library = nullptr;
    void *pcm = nullptr;
    uint32_t configured_rate = 0;
    bool poisoned = false;
    int (*open)(void **, const char *, int, int) = nullptr;
    int (*close)(void *) = nullptr;
    int (*set_params)(void *, int, int, unsigned, unsigned, int, unsigned) = nullptr;
    int (*prepare)(void *) = nullptr;
    int (*drop)(void *) = nullptr;
    int (*drain)(void *) = nullptr;
    int (*wait)(void *, int) = nullptr;
    long (*write)(void *, const void *, unsigned long) = nullptr;
    const char *(*strerror)(int) = nullptr;
};
// The firmware audio worker has process lifetime; never tear down a live sink.
AlsaOutput *const s_output = new (std::nothrow) AlsaOutput{};

// target: Receives one public alsa-lib ABI entry.
// library: Successfully opened shared object.
// name: Exact documented symbol.
// Exception: Returns false on a missing symbol; no fallback implementation is invented.
template <typename T> bool load_symbol(T &target, void *library, const char *name)
{
    target = reinterpret_cast<T>(dlsym(library, name));
    return target != nullptr;
}

// state: Host output owner with the original ALSA error decoder.
// operation: Exact failing ALSA operation.
// result: Original negative ALSA error, preserved in the log.
// Exception: Returns a host error without converting failed submission into playback.
esp_err_t failure(AlsaOutput &state, const char *operation, int result)
{
    ESP_LOGE(kTag, "%s failed: ALSA=%d (%s)", operation, result,
        state.strerror ? state.strerror(result) : "unavailable");
    return ESP_FAIL;
}

// state: Exclusive ALSA owner.
// rate: Exact mixer-selected sample rate.
// Exception: Returns the ALSA format setup error; no output occurs.
esp_err_t ensure_format(AlsaOutput &state, uint32_t rate)
{
    if (state.poisoned || !state.pcm) return ESP_ERR_INVALID_STATE;
    if (state.configured_rate == rate) return ESP_OK;
    const int result = state.set_params(state.pcm, kS16LittleEndian, kInterleaved,
        1, rate, 1, 40000);
    if (result < 0) return failure(state, "snd_pcm_set_params", result);
    state.configured_rate = rate;
    return ESP_OK;
}

// state: Exclusive ALSA owner whose stream is being abandoned after an error.
// Exception: Preserves a failed reset as a poisoned output route.
void abandon(AlsaOutput &state)
{
    const int result = state.drop(state.pcm);
    if (result < 0) {
        state.poisoned = true;
        (void)failure(state, "snd_pcm_drop", result);
        // If the device cannot stop the queued frames, close the route rather
        // than treating an interrupted native cue as successfully muted.
        const int closed = state.close(state.pcm);
        if (closed < 0) (void)failure(state, "snd_pcm_close", closed);
        else state.pcm = nullptr;
    }
}

// samples: Borrowed PCM from the native mixer, valid until this call returns.
// count: Number of mono frames, bounded by the real owner's 20 ms write size.
// rate: Native selected playback rate.
// volume: Native codec volume, 0..100.
// muted: Native codec mute state.
// context: Exclusive ALSA output owner.
// Exception: Returns ALSA errors or timeout; drain is awaited before completion.
esp_err_t write_pcm(const int16_t *samples, std::size_t count, uint32_t rate,
    int volume, bool muted, void *context)
{
    if (!samples || !context || !count || count > 960U || rate < 8000 || rate > 48000 ||
        volume < 0 || volume > 100) return ESP_ERR_INVALID_ARG;
    auto &state = *static_cast<AlsaOutput *>(context);
    std::lock_guard<std::mutex> guard(state.mutex);
    auto error = ensure_format(state, rate);
    if (error != ESP_OK) return error;
    int result = state.prepare(state.pcm);
    if (result < 0) return failure(state, "snd_pcm_prepare", result);
    // The ES8311 applies mute/volume after mixing. The host codec applies that
    // output setting to the exact native PCM; it does not synthesize a cue.
    std::array<uint8_t, 1920> pcm{};
    for (std::size_t i = 0; i < count; ++i) {
        const int32_t scaled = muted ? 0 : static_cast<int32_t>(samples[i]) * volume / 100;
        const uint16_t bits = static_cast<uint16_t>(static_cast<int16_t>(scaled));
        pcm[i * 2] = static_cast<uint8_t>(bits);
        pcm[i * 2 + 1] = static_cast<uint8_t>(bits >> 8U);
    }
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds((count * 1000U + rate - 1U) / rate + 2000U);
    std::size_t written = 0;
    while (written < count) {
        const long frames = state.write(state.pcm, pcm.data() + written * 2U,
            static_cast<unsigned long>(count - written));
        if (frames > 0 && static_cast<unsigned long>(frames) <= count - written) {
            written += static_cast<std::size_t>(frames);
            continue;
        }
        if (frames < 0 && frames != -EAGAIN) { abandon(state); return failure(state, "snd_pcm_writei", static_cast<int>(frames)); }
        if (frames > 0) { abandon(state); return ESP_ERR_INVALID_SIZE; }
        if (std::chrono::steady_clock::now() >= deadline) { abandon(state); return ESP_ERR_TIMEOUT; }
        result = state.wait(state.pcm, 20);
        if (result < 0 && result != -EAGAIN) { abandon(state); return failure(state, "snd_pcm_wait", result); }
    }
    for (;;) {
        result = state.drain(state.pcm);
        if (result == 0) return ESP_OK;
        if (result != -EAGAIN) { abandon(state); return failure(state, "snd_pcm_drain", result); }
        if (std::chrono::steady_clock::now() >= deadline) { abandon(state); return ESP_ERR_TIMEOUT; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
}

// No parameters: explicitly installs an ALSA sink only when its library and default device exist.
// Exception: Missing library/symbol/device returns NOT_SUPPORTED; no playback is requested.
esp_err_t cody_host_audio_use_alsa()
{
    if (!s_output) return ESP_ERR_NO_MEM;
    auto &state = *s_output;
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.library || state.pcm) return ESP_ERR_INVALID_STATE;
    state.library = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!state.library) return ESP_ERR_NOT_SUPPORTED;
    const bool symbols = load_symbol(state.open, state.library, "snd_pcm_open") &&
        load_symbol(state.close, state.library, "snd_pcm_close") &&
        load_symbol(state.set_params, state.library, "snd_pcm_set_params") &&
        load_symbol(state.prepare, state.library, "snd_pcm_prepare") &&
        load_symbol(state.drop, state.library, "snd_pcm_drop") &&
        load_symbol(state.drain, state.library, "snd_pcm_drain") &&
        load_symbol(state.wait, state.library, "snd_pcm_wait") &&
        load_symbol(state.write, state.library, "snd_pcm_writei") &&
        load_symbol(state.strerror, state.library, "snd_strerror");
    if (!symbols) { dlclose(state.library); state.library = nullptr; return ESP_ERR_NOT_SUPPORTED; }
    const int opened = state.open(&state.pcm, "default", kPlayback, kNonblocking);
    if (opened < 0) {
        (void)failure(state, "snd_pcm_open", opened);
        dlclose(state.library); state.library = nullptr; state.pcm = nullptr;
        return opened == -ENOENT || opened == -ENODEV ? ESP_ERR_NOT_SUPPORTED : ESP_FAIL;
    }
    const auto error = cody_host_audio_register_sink(write_pcm, &state);
    if (error != ESP_OK) {
        const int closed = state.close(state.pcm);
        if (closed < 0) (void)failure(state, "snd_pcm_close", closed);
        else { state.pcm = nullptr; dlclose(state.library); state.library = nullptr; }
    }
    return error;
}
#else
// No parameters: this source does not claim a Windows ALSA implementation.
esp_err_t cody_host_audio_use_alsa() { return ESP_ERR_NOT_SUPPORTED; }
#endif
