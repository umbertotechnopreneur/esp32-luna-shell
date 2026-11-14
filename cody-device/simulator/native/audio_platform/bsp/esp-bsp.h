#pragma once

#include "esp_err.h"
#include "esp_codec_dev.h"

// Host-only output adapter; no physical board is opened by these functions.
esp_err_t bsp_audio_codec_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone);
esp_err_t bsp_audio_codec_deinit(void);
esp_err_t bsp_audio_set_transport_standby(bool standby, bool resume_tx, bool resume_rx);
