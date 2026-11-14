#pragma once

#include <stdbool.h>
#include <stdint.h>

// Host-only codec contract used by the unmodified firmware audio owner.
typedef void *esp_codec_dev_handle_t;
typedef struct {
    uint8_t bits_per_sample;
    uint8_t channel;
    uint16_t channel_mask;
    uint32_t sample_rate;
    uint32_t mclk_multiple;
} esp_codec_dev_sample_info_t;

#define ESP_CODEC_DEV_OK 0

int esp_codec_dev_open(esp_codec_dev_handle_t codec, esp_codec_dev_sample_info_t *format);
int esp_codec_dev_close(esp_codec_dev_handle_t codec);
int esp_codec_dev_write(esp_codec_dev_handle_t codec, void *data, int length);
int esp_codec_dev_read(esp_codec_dev_handle_t codec, void *data, int length);
int esp_codec_dev_get_out_vol(esp_codec_dev_handle_t codec, int *volume);
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t codec, int volume);
int esp_codec_dev_get_out_mute(esp_codec_dev_handle_t codec, bool *muted);
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t codec, bool muted);
int esp_codec_dev_get_in_gain(esp_codec_dev_handle_t codec, float *gain);
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t codec, float gain);
int esp_codec_dev_get_in_mute(esp_codec_dev_handle_t codec, bool *muted);
int esp_codec_dev_set_in_mute(esp_codec_dev_handle_t codec, bool muted);
