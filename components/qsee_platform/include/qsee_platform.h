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

typedef struct {
    bool clock_integrity;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} qsee_calendar_time_t;

/**
 * Probe and initialize hardware attached to the Waveshare BSP I2C bus.
 *
 * Every peripheral is fail-open: a missing optional sensor is logged but does
 * not prevent the application from starting.
 */
esp_err_t qsee_hardware_init(void);

/**
 * Refresh the canonical hardware snapshot once. No background polling occurs.
 */
esp_err_t qsee_hardware_refresh(const char *reason);

/**
 * Run an explicit stationary QMI8658 calibration.
 *
 * The call rejects moving sample sets and keeps the previous valid calibration.
 */
esp_err_t qsee_imu_calibrate(void);

/** Read or write the board PCF85063. The RTC is maintained in UTC. */
esp_err_t qsee_rtc_read(qsee_calendar_time_t *time);
esp_err_t qsee_rtc_write(const qsee_calendar_time_t *time);

/** Restore system time from RTC and prepare deferred SNTP synchronization. */
esp_err_t qsee_time_init(const char *timezone);

/** Start SNTP after DHCP has supplied an IP address. */
esp_err_t qsee_time_notify_wifi_online(void);

/** Apply a valid UTC GPS date/time to system time and the RTC. */
esp_err_t qsee_time_sync_from_gps(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
);

/** Initialize BOOT/PWR observation, PMU events, wake-cause and TCA9554 health. */
esp_err_t qsee_power_inputs_init(void);

/** Enter explicit low-power states; callers own the surrounding UI lifecycle. */
esp_err_t qsee_power_enter_light_sleep(uint64_t wake_after_us);
esp_err_t qsee_power_enter_deep_sleep(uint64_t wake_after_us);

/** Start non-blocking SD detection/mount retries. Never formats automatically. */
esp_err_t qsee_storage_init(void);

/** Arm one format attempt after explicit user authorization. No UI is implied. */
esp_err_t qsee_storage_request_format(void);
bool qsee_storage_mounted(void);

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
esp_err_t qsee_ble_init(void);

/** Start the fail-open hot-plug SD storage service. */
esp_err_t qsee_storage_start(void);

/** Request formatting through the storage service, never directly from UI. */
esp_err_t qsee_storage_request_format(void);

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

/** Start/stop secure stock ESP-IDF BLE Wi-Fi provisioning. */
esp_err_t qsee_provisioning_init(void);
esp_err_t qsee_provisioning_start(void);
esp_err_t qsee_provisioning_stop(void);
bool qsee_provisioning_active(void);
const char *qsee_provisioning_service_name(void);
const char *qsee_provisioning_pop(void);

#ifdef __cplusplus
}
#endif
