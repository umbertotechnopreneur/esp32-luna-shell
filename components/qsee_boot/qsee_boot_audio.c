#include "qsee_boot.h"
#include "qsee_platform.h"

#include <stddef.h>
#include <stdint.h>

#include "bsp/esp-bsp.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const uint8_t qsee_boot_wav_start[] asm("_binary_qsee_boot_wav_start");
extern const uint8_t qsee_boot_wav_end[] asm("_binary_qsee_boot_wav_end");

static const char *TAG = "qsee_audio";
static StaticTask_t s_audio_task_control;
static StackType_t s_audio_task_stack[3072];

static void audio_task(void *arg)
{
    (void)arg;
    const size_t wav_size = (size_t)(qsee_boot_wav_end - qsee_boot_wav_start);
    if (wav_size <= 44U) {
        qsee_hardware_report_speaker(false, ESP_ERR_INVALID_SIZE);
        ESP_LOGW(TAG, "Boot SFX asset is invalid");
        vTaskDelete(NULL);
        return;
    }

    esp_codec_dev_handle_t speaker = bsp_audio_codec_speaker_init();
    if (speaker == NULL) {
        qsee_hardware_report_speaker(false, ESP_ERR_NOT_FOUND);
        ESP_LOGW(TAG, "Speaker codec unavailable");
        vTaskDelete(NULL);
        return;
    }

    esp_codec_dev_sample_info_t sample = {
        .sample_rate = 22050,
        .channel = 1,
        .bits_per_sample = 16,
    };
    int result = esp_codec_dev_open(speaker, &sample);
    if (result == ESP_CODEC_DEV_OK) {
        (void)esp_codec_dev_set_out_vol(speaker, 65);
        result = esp_codec_dev_write(speaker, (void *)(qsee_boot_wav_start + 44), wav_size - 44U);
        (void)esp_codec_dev_close(speaker);
    }

    if (result == ESP_CODEC_DEV_OK) {
        qsee_hardware_report_speaker(true, ESP_OK);
        ESP_LOGI(TAG, "Boot SFX played: 22.05kHz mono PCM16");
    } else {
        qsee_hardware_report_speaker(false, ESP_FAIL);
        ESP_LOGW(TAG, "Boot SFX playback failed (%d)", result);
    }
    vTaskDelete(NULL);
}

esp_err_t qsee_boot_audio_play(void)
{
    TaskHandle_t task = xTaskCreateStatic(
        audio_task,
        "qsee_boot_sfx",
        sizeof(s_audio_task_stack) / sizeof(s_audio_task_stack[0]),
        NULL,
        3,
        s_audio_task_stack,
        &s_audio_task_control
    );
    return task != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}
