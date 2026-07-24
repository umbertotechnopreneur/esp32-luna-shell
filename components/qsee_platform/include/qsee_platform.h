/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * QSee.ai: https://qsee.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@qsee.ai
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Probe and initialize hardware attached to the Waveshare BSP I2C bus.
 *
 * Every peripheral is fail-open: a missing optional sensor is logged but does
 * not prevent the UI from starting.
 */
esp_err_t qsee_hardware_init(void);

/**
 * Refresh the canonical hardware snapshot once. No background polling occurs.
 */
esp_err_t qsee_hardware_refresh(const char *reason);

/**
 * Report the result of an actual speaker playback attempt.
 */
void qsee_hardware_report_speaker(bool ready, esp_err_t error);

/**
 * Initialize the shared ES8311 speaker, ES7210 microphone and I2S transport.
 * The codec handles remain open so playback and capture use one canonical route.
 */
esp_err_t qsee_audio_init(void);

/** Play mono signed PCM16 through the board speaker. */
esp_err_t qsee_audio_play_pcm16(
    const int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz,
    int volume_percent
);

/** Capture mono signed PCM16 from the board microphone array. */
esp_err_t qsee_audio_capture_pcm16(
    int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz,
    float gain_db
);

/**
 * Start the Wi-Fi station lifecycle and BLE advertising as QSEE-xxxx.
 */
esp_err_t qsee_radios_init(void);

#define QSEE_WIFI_MAX_NETWORKS 16
#define QSEE_WIFI_SSID_MAX_LEN 32

typedef struct {
    char ssid[QSEE_WIFI_SSID_MAX_LEN + 1];
    int8_t rssi;
    uint8_t channel;
    bool secured;
} qsee_wifi_network_t;

typedef struct {
    bool initialized;
    bool started;
    bool scanning;
    bool credentials_configured;
    bool connect_requested;
    bool connected;
    bool ip_acquired;
    bool reconnect_pending;
    int8_t rssi;
    uint8_t channel;
    uint16_t last_disconnect_reason;
    uint32_t event_sequence;
    uint32_t connect_attempt_count;
    uint32_t reconnect_attempt_count;
    uint32_t disconnect_count;
    uint32_t reconnect_delay_ms;
    esp_err_t last_error;
    char ssid[QSEE_WIFI_SSID_MAX_LEN + 1];
    char ip_address[16];
    char netmask[16];
    char gateway[16];
    char mac_address[18];
} qsee_wifi_status_t;

/**
 * Initialize one event-driven ESP-IDF station lifecycle.
 *
 * A profile previously stored by ESP-IDF is connected automatically.
 */
esp_err_t qsee_wifi_init(void);

/**
 * Apply a station profile and connect. The password is never written to logs.
 *
 * When persist is true ESP-IDF stores the profile in NVS.
 */
esp_err_t qsee_wifi_connect(const char *ssid, const char *password, bool persist);

/**
 * Stop reconnect attempts while leaving the station driver available.
 */
esp_err_t qsee_wifi_disconnect(void);

/**
 * Start one asynchronous all-channel scan.
 */
esp_err_t qsee_wifi_start_scan(void);

/**
 * Copy fixed scan results and the immutable connection state.
 */
size_t qsee_wifi_get_networks(qsee_wifi_network_t *networks, size_t capacity);
void qsee_wifi_get_status(qsee_wifi_status_t *status);

#ifdef __cplusplus
}
#endif
