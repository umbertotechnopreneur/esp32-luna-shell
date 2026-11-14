#include "CODY_host_audio_codec.hpp"
#include "bsp/esp-bsp.h"
#include "LUNASHELL_audio_controls.h"
#include "LUNASHELL_platform.h"

#include <mutex>

namespace {
struct HostCodec {
    bool acquired = false;
    bool opened = false;
    bool standby = false;
    bool muted = true;
    int volume = 90;
    uint32_t rate = 0;
    CodyHostPcmSink sink = nullptr;
    void *context = nullptr;
};
HostCodec s_codec{};
std::mutex s_codec_mutex;

// codec: Borrowed host handle; never accepts a physical hardware pointer.
// Exception: None; this helper checks identity while its caller owns the mutex.
bool valid(esp_codec_dev_handle_t codec) { return codec == &s_codec && s_codec.acquired; }
}

// sink: Explicit synchronous host PCM output implementation.
// context: Borrowed owner retained until the audio route is deinitialized.
// Exception: Rejects null, replacement, or registration while the route is acquired.
esp_err_t cody_host_audio_register_sink(CodyHostPcmSink sink, void *context)
{
    std::lock_guard<std::mutex> lock(s_codec_mutex);
    if (!sink) return ESP_ERR_INVALID_ARG;
    if (s_codec.acquired || s_codec.sink) return ESP_ERR_INVALID_STATE;
    // Register before the native settings owner starts: unknown policy must
    // not inherit the firmware audio owner's permissive pre-Cody defaults.
    LUNASHELL_audio_begin_cody_output_policy();
    const auto error = LUNASHELL_audio_set_user_sounds_enabled(false);
    if (error != ESP_OK) return error;
    s_codec.sink = sink;
    s_codec.context = context;
    return ESP_OK;
}

// speaker: Receives the single simulated codec handle on success.
// microphone: Receives null because this host backend models output only.
// Exception: Missing sink fails closed; output pointers are never partially admitted.
esp_err_t bsp_audio_codec_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone)
{
    if (!speaker || !microphone) return ESP_ERR_INVALID_ARG;
    *speaker = nullptr;
    *microphone = nullptr;
    std::lock_guard<std::mutex> lock(s_codec_mutex);
    if (!s_codec.sink) return ESP_ERR_NOT_SUPPORTED;
    if (s_codec.acquired) return ESP_ERR_INVALID_STATE;
    s_codec.acquired = true;
    s_codec.opened = false;
    s_codec.standby = false;
    s_codec.muted = true;
    s_codec.rate = 0;
    *speaker = &s_codec;
    return ESP_OK;
}

// No parameters: releases the virtual route only after the firmware owner closes it.
// Exception: Refuses release while the simulated output stream is open.
esp_err_t bsp_audio_codec_deinit(void)
{
    std::lock_guard<std::mutex> lock(s_codec_mutex);
    if (!s_codec.acquired || s_codec.opened) return ESP_ERR_INVALID_STATE;
    s_codec.acquired = false;
    return ESP_OK;
}

// standby: Whether the native audio owner suspends its transport.
// resume_tx: Firmware-requested TX restoration, handled by the owner's reopen.
// resume_rx: Unsupported microphone route; true is rejected.
// Exception: Returns invalid state when no virtual route is owned.
esp_err_t bsp_audio_set_transport_standby(bool standby, bool resume_tx, bool resume_rx)
{
    (void)resume_tx;
    std::lock_guard<std::mutex> lock(s_codec_mutex);
    if (!s_codec.acquired) return ESP_ERR_INVALID_STATE;
    if (resume_rx) return ESP_ERR_NOT_SUPPORTED;
    s_codec.standby = standby;
    return ESP_OK;
}

// codec: The exclusive simulated speaker handle.
// format: Native owner's complete mono PCM format.
// Exception: Rejects invalid format, unavailable sink, or a suspended route.
int esp_codec_dev_open(esp_codec_dev_handle_t codec, esp_codec_dev_sample_info_t *format)
{
    std::lock_guard<std::mutex> lock(s_codec_mutex);
    if (!valid(codec) || s_codec.standby || !s_codec.sink) return ESP_ERR_INVALID_STATE;
    if (!format || format->bits_per_sample != 16 || format->channel != 1 ||
        format->sample_rate < 8000 || format->sample_rate > 48000) return ESP_ERR_INVALID_ARG;
    if (s_codec.opened) return ESP_ERR_INVALID_STATE;
    s_codec.rate = format->sample_rate;
    s_codec.opened = true;
    return ESP_OK;
}

// codec: The exclusive simulated speaker handle.
// Exception: Rejects a missing route or stream without altering state.
int esp_codec_dev_close(esp_codec_dev_handle_t codec)
{
    std::lock_guard<std::mutex> lock(s_codec_mutex);
    if (!valid(codec) || !s_codec.opened) return ESP_ERR_INVALID_STATE;
    s_codec.opened = false;
    s_codec.rate = 0;
    return ESP_OK;
}

// codec: The exclusive simulated speaker handle.
// data: Firmware mixer-owned mono PCM16, valid only during this call.
// length: Exact byte count, necessarily even and positive.
// Exception: Propagates sink backpressure/errors and rejects unavailable routes.
int esp_codec_dev_write(esp_codec_dev_handle_t codec, void *data, int length)
{
    if (!data || length <= 0 || length % static_cast<int>(sizeof(int16_t)) != 0)
        return ESP_ERR_INVALID_ARG;
    CodyHostPcmSink sink = nullptr;
    void *context = nullptr;
    uint32_t rate = 0;
    int volume = 0;
    bool muted = true;
    {
        std::lock_guard<std::mutex> lock(s_codec_mutex);
        if (!valid(codec) || !s_codec.opened || s_codec.standby || !s_codec.sink)
            return ESP_ERR_INVALID_STATE;
        sink = s_codec.sink;
        context = s_codec.context;
        rate = s_codec.rate;
        volume = s_codec.volume;
        muted = s_codec.muted;
    }
    return sink(static_cast<const int16_t *>(data), static_cast<std::size_t>(length) / sizeof(int16_t),
        rate, volume, muted, context);
}

// codec: The exclusive simulated speaker handle.
// volume: Receives or supplies the native output setting in the range 0..100.
// Exception: Rejects invalid handles, values, or output pointers.
int esp_codec_dev_get_out_vol(esp_codec_dev_handle_t codec, int *volume)
{
    std::lock_guard<std::mutex> lock(s_codec_mutex);
    if (!valid(codec) || !volume) return ESP_ERR_INVALID_ARG;
    *volume = s_codec.volume;
    return ESP_OK;
}

// codec: The exclusive simulated speaker handle.
// volume: The native output setting in the range 0..100.
// Exception: Rejects invalid handles or volume values.
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t codec, int volume)
{
    std::lock_guard<std::mutex> lock(s_codec_mutex);
    if (!valid(codec) || volume < 0 || volume > 100) return ESP_ERR_INVALID_ARG;
    s_codec.volume = volume;
    return ESP_OK;
}

// codec: The exclusive simulated speaker handle.
// muted: Receives or supplies the firmware owner's codec mute state.
// Exception: Rejects invalid handles or output pointers.
int esp_codec_dev_get_out_mute(esp_codec_dev_handle_t codec, bool *muted)
{
    std::lock_guard<std::mutex> lock(s_codec_mutex);
    if (!valid(codec) || !muted) return ESP_ERR_INVALID_ARG;
    *muted = s_codec.muted;
    return ESP_OK;
}

// codec: The exclusive simulated speaker handle.
// muted: The firmware owner's codec mute state.
// Exception: Rejects an invalid handle.
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t codec, bool muted)
{
    std::lock_guard<std::mutex> lock(s_codec_mutex);
    if (!valid(codec)) return ESP_ERR_INVALID_ARG;
    s_codec.muted = muted;
    return ESP_OK;
}

// codec: A host codec handle, never treated as a microphone.
// data: Unused caller buffer.
// length: Unused requested byte count.
// Exception: Always returns NOT_SUPPORTED; no input samples are fabricated.
int esp_codec_dev_read(esp_codec_dev_handle_t, void *, int) { return ESP_ERR_NOT_SUPPORTED; }
// codec: A host codec handle, never treated as a microphone.
// gain: Unused output pointer.
// Exception: Always returns NOT_SUPPORTED without changing the output.
int esp_codec_dev_get_in_gain(esp_codec_dev_handle_t, float *) { return ESP_ERR_NOT_SUPPORTED; }
// codec: A host codec handle, never treated as a microphone.
// gain: Unused requested gain.
// Exception: Always returns NOT_SUPPORTED without changing state.
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t, float) { return ESP_ERR_NOT_SUPPORTED; }
// codec: A host codec handle, never treated as a microphone.
// muted: Unused output pointer.
// Exception: Always returns NOT_SUPPORTED without changing the output.
int esp_codec_dev_get_in_mute(esp_codec_dev_handle_t, bool *) { return ESP_ERR_NOT_SUPPORTED; }
// codec: A host codec handle, never treated as a microphone.
// muted: Unused requested input mute.
// Exception: Always returns NOT_SUPPORTED without changing state.
int esp_codec_dev_set_in_mute(esp_codec_dev_handle_t, bool) { return ESP_ERR_NOT_SUPPORTED; }
