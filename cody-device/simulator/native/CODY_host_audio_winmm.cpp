#include "CODY_host_audio_codec.hpp"
#include "esp_log.h"

#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <vector>

namespace {
constexpr const char *kTag = "CodyHostWinMM";
struct PendingBlock {
    WAVEHDR header{};
    std::vector<int16_t> samples;
};
struct WaveOutput {
    std::mutex mutex;
    HWAVEOUT device = nullptr;
    HANDLE completed = nullptr;
    uint32_t rate = 0;
    bool poisoned = false;
    // If WinMM refuses cleanup, this allocation stays valid until process exit.
    std::unique_ptr<PendingBlock> pending;
};
// The firmware worker has process lifetime and can outlive ordinary static destructors.
WaveOutput *const s_output = new (std::nothrow) WaveOutput{};

// operation: Exact failed WinMM operation, retained in the host log.
// result: Original WinMM result, which is not an ESP-IDF error identity.
// Exception: Returns ESP_FAIL without treating submission as audible completion.
esp_err_t failure(const char *operation, MMRESULT result)
{
    ESP_LOGE(kTag, "%s failed: WinMM=%u", operation, static_cast<unsigned>(result));
    return ESP_FAIL;
}

// state: Exclusive host wave-output owner.
// rate: Exact native mixer sample rate.
// Exception: Preserves device-open/close failures; no PCM is submitted.
esp_err_t ensure_open(WaveOutput &state, uint32_t rate)
{
    if (state.poisoned) return ESP_ERR_INVALID_STATE;
    if (state.device && state.rate == rate) return ESP_OK;
    if (state.device) {
        const MMRESULT result = waveOutClose(state.device);
        if (result != MMSYSERR_NOERROR) { state.poisoned = true; return failure("waveOutClose", result); }
        state.device = nullptr;
        state.rate = 0;
    }
    if (!state.completed) {
        state.completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!state.completed) {
            ESP_LOGE(kTag, "CreateEvent failed: Windows=%lu", static_cast<unsigned long>(GetLastError()));
            return ESP_FAIL;
        }
    }
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = rate * 2U;
    const MMRESULT result = waveOutOpen(&state.device, WAVE_MAPPER, &format,
        reinterpret_cast<DWORD_PTR>(state.completed), 0, CALLBACK_EVENT);
    if (result != MMSYSERR_NOERROR) { state.device = nullptr; return failure("waveOutOpen", result); }
    state.rate = rate;
    return ESP_OK;
}

// samples: Borrowed native mixer PCM, retained by the caller until return.
// count: Exact number of mono samples in this block.
// rate: Native selected playback rate.
// volume: Native codec volume, 0..100.
// muted: Native codec mute state.
// context: Exclusive WinMM output owner.
// Exception: Returns the original route/queue/timeout failure; completion is awaited.
esp_err_t write_pcm(const int16_t *samples, std::size_t count, uint32_t rate,
    int volume, bool muted, void *context)
{
    if (!samples || !context || count == 0 || count > 960U ||
        rate < 8000 || rate > 48000 || volume < 0 || volume > 100) return ESP_ERR_INVALID_ARG;
    auto &state = *static_cast<WaveOutput *>(context);
    std::lock_guard<std::mutex> guard(state.mutex);
    auto error = ensure_open(state, rate);
    if (error != ESP_OK) return error;
    const DWORD channel = muted ? 0U : static_cast<DWORD>((static_cast<uint64_t>(volume) * 65535U) / 100U);
    const MMRESULT volume_result = waveOutSetVolume(state.device, channel | (channel << 16U));
    if (volume_result != MMSYSERR_NOERROR) return failure("waveOutSetVolume", volume_result);
    try {
        state.pending = std::make_unique<PendingBlock>();
        state.pending->samples.assign(samples, samples + count);
    } catch (const std::bad_alloc &) {
        state.pending.reset();
        return ESP_ERR_NO_MEM;
    }
    auto &header = state.pending->header;
    header.lpData = reinterpret_cast<LPSTR>(state.pending->samples.data());
    header.dwBufferLength = static_cast<DWORD>(count * sizeof(int16_t));
    MMRESULT result = waveOutPrepareHeader(state.device, &header, sizeof(header));
    if (result != MMSYSERR_NOERROR) { state.pending.reset(); return failure("waveOutPrepareHeader", result); }
    result = waveOutWrite(state.device, &header, sizeof(header));
    if (result != MMSYSERR_NOERROR) {
        const MMRESULT cleanup = waveOutUnprepareHeader(state.device, &header, sizeof(header));
        if (cleanup != MMSYSERR_NOERROR) { state.poisoned = true; (void)failure("waveOutUnprepareHeader", cleanup); }
        else state.pending.reset();
        return failure("waveOutWrite", result);
    }
    const uint64_t block_ms = (static_cast<uint64_t>(count) * 1000U + rate - 1U) / rate;
    const ULONGLONG deadline = GetTickCount64() + block_ms + 2000U;
    while ((header.dwFlags & WHDR_DONE) == 0) {
        const ULONGLONG now = GetTickCount64();
        const DWORD waited = now >= deadline ? WAIT_TIMEOUT : WaitForSingleObject(state.completed,
            static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, MAXDWORD - 1U)));
        if (waited != WAIT_OBJECT_0) {
            if (waited == WAIT_FAILED)
                ESP_LOGE(kTag, "WaitForSingleObject failed: Windows=%lu", static_cast<unsigned long>(GetLastError()));
            const MMRESULT reset = waveOutReset(state.device);
            if (reset != MMSYSERR_NOERROR) { state.poisoned = true; (void)failure("waveOutReset", reset); }
            const MMRESULT cleanup = waveOutUnprepareHeader(state.device, &header, sizeof(header));
            if (cleanup != MMSYSERR_NOERROR) { state.poisoned = true; (void)failure("waveOutUnprepareHeader", cleanup); }
            else state.pending.reset();
            return waited == WAIT_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
        }
    }
    result = waveOutUnprepareHeader(state.device, &header, sizeof(header));
    if (result != MMSYSERR_NOERROR) { state.poisoned = true; return failure("waveOutUnprepareHeader", result); }
    state.pending.reset();
    return ESP_OK;
}
}

// No parameters: installs the WinMM PCM sink; opening the device waits until the first native block.
// Exception: Returns NOT_SUPPORTED when Windows reports no audio device, or the original registration error.
esp_err_t cody_host_audio_use_winmm()
{
    if (waveOutGetNumDevs() == 0) return ESP_ERR_NOT_SUPPORTED;
    if (!s_output) return ESP_ERR_NO_MEM;
    return cody_host_audio_register_sink(write_pcm, s_output);
}
#else
// No parameters: this source does not claim a Linux streaming implementation.
esp_err_t cody_host_audio_use_winmm() { return ESP_ERR_NOT_SUPPORTED; }
#endif
