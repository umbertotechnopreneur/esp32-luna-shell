/*
 * Firmware backend support module.
 *
 * Copyright (c) 2026
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
 * Returns the original initialization error from the first failing peripheral.
 * Any decision to continue after an optional-device failure belongs to the
 * caller; this platform layer never converts a partial failure into success.
 */
esp_err_t qsee_hardware_init(void);

/** Refresh the canonical hardware snapshot once. */
esp_err_t qsee_hardware_refresh(const char *reason);

/**
 * Start or stop the periodic Full Snapshot worker.
 *
 * The worker serializes complete hardware refreshes and publishes every result
 * through DeviceSnapshotService. It is independent from diagnostic sampling.
 */
esp_err_t qsee_hardware_start_full_snapshot_monitor(void);
esp_err_t qsee_hardware_stop_full_snapshot_monitor(void);
/** Suppress periodic Full Snapshots without changing the requested lifecycle. */
esp_err_t qsee_hardware_set_full_snapshot_performance_suspended(bool suspended);
/** Persist one supported Full Snapshot cadence: 300-10000 ms presets only. */
esp_err_t qsee_hardware_set_full_snapshot_period_ms(uint32_t period_ms);

/** Enable or disable the periodic snapshot monitor used by diagnostic pages. */
esp_err_t qsee_hardware_set_diagnostic_monitor(bool enabled);
/**
 * Enable or disable lightweight IMU publication for adaptive UI wake.
 *
 * The monitor publishes only measured IMU samples through DeviceSnapshotService;
 * it does not poll microphones or expose a second sensor-data path to the UI.
 */
esp_err_t qsee_hardware_set_ui_motion_monitor(bool enabled);
/** Configure the active diagnostic sample period (100-2000 ms). */
esp_err_t qsee_hardware_set_diagnostic_period_ms(uint32_t period_ms);
/** Return the current diagnostic sample period in milliseconds. */
uint32_t qsee_hardware_get_diagnostic_period_ms(void);
/** Configure low-pass smoothing of calibrated IMU input (0-90 percent). */
esp_err_t qsee_hardware_set_imu_smoothing_percent(uint32_t percent);
/** Return the persisted IMU input smoothing percentage. */
uint32_t qsee_hardware_get_imu_smoothing_percent(void);

/**
 * Schedule an explicit stationary QMI8658 calibration on the platform worker.
 *
 * The request rejects moving sample sets, keeps the previous valid calibration,
 * and returns immediately once the worker accepts the calibration.
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

typedef enum {
    QSEE_POWER_TIMEOUT_COMMAND_ARC = 0,
    QSEE_POWER_TIMEOUT_STATUS_ARC = 1,
    QSEE_POWER_TIMEOUT_DISPLAY_DIM = 2,
    QSEE_POWER_TIMEOUT_DISPLAY_OFF = 3,
} qsee_power_timeout_t;

/** Start the single BOOT/PWR, inactivity, display, audio, and IMU service. */
esp_err_t qsee_power_manager_init(void);

/** Record activity without blocking the touch or UI callback. */
void qsee_power_manager_note_activity(void);

/** Persist and apply one supported inactivity timeout. */
esp_err_t qsee_power_manager_set_timeout(
    qsee_power_timeout_t timeout,
    uint32_t seconds
);

/** Read one active inactivity timeout from the power service. */
esp_err_t qsee_power_manager_get_timeout(
    qsee_power_timeout_t timeout,
    uint32_t *seconds
);

/** Enter explicit low-power states; callers own the surrounding UI lifecycle. */
esp_err_t qsee_power_enter_light_sleep(uint64_t wake_after_us);
esp_err_t qsee_power_enter_deep_sleep(uint64_t wake_after_us);

/** Start non-blocking SD detection/mount retries. Never formats automatically. */
esp_err_t qsee_storage_init(void);

/**
 * Arm one immediate format attempt after explicit user authorization.
 *
 * The authorization is accepted only for the exact card identity from the
 * latest verified FR_NO_FILESYSTEM probe and expires when consumed.
 */
esp_err_t qsee_storage_request_format(void);
esp_err_t qsee_storage_set_logger_mirror(bool enabled);
/** Pause effective SD log writes while preserving the user's saved preference. */
esp_err_t qsee_storage_set_performance_suspended(bool suspended);

/** Persist and apply AMOLED brightness (10-100 percent). */
esp_err_t qsee_display_set_brightness_percent(uint32_t percent);
esp_err_t qsee_display_get_brightness_percent(uint32_t *percent);
esp_err_t qsee_display_apply_saved_brightness(void);

/** Persist the automatic cardinal-rotation preference. */
esp_err_t qsee_display_set_auto_rotation_enabled(bool enabled);

/** Return the active automatic cardinal-rotation preference. */
bool qsee_display_get_auto_rotation_enabled(void);

/** Restore the persisted preference and last successfully applied rotation. */
esp_err_t qsee_display_apply_saved_auto_rotation(void);

typedef enum {
    QSEE_DISPLAY_POWER_ACTIVE = 0,
    QSEE_DISPLAY_POWER_DIMMED = 1,
    QSEE_DISPLAY_POWER_OFF = 2,
} qsee_display_power_mode_t;

/** Apply a transient display power mode without changing persisted brightness. */
esp_err_t qsee_display_set_power_mode(qsee_display_power_mode_t mode);

/** Fade to a transient display power mode without changing saved brightness. */
esp_err_t qsee_display_fade_to_power_mode(
    qsee_display_power_mode_t mode,
    uint32_t duration_ms
);

/**
 * Report the result of an actual speaker playback attempt.
 */
void qsee_hardware_report_speaker(bool ready, esp_err_t error);

/** Initialize the selected speaker and microphone codec paths. */
esp_err_t qsee_audio_init(void);

/** Return the number of physically selected microphone channels: 0, 1, or 2. */
uint8_t qsee_audio_input_channel_count(void);

/** Play mono signed PCM16 through the board speaker. */
esp_err_t qsee_audio_play_pcm16(
    const int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz,
    int volume_percent
);

/** Capture mono signed PCM16 when the selected board exposes one microphone. */
esp_err_t qsee_audio_capture_pcm16(
    int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz,
    float gain_db
);

/** Capture interleaved PCM16 frames from the ES7210 microphone channels. */
esp_err_t qsee_audio_capture_pcm16_channels(
    int16_t *samples,
    size_t frame_count,
    uint8_t channel_count,
    uint32_t sample_rate_hz,
    float gain_db
);

/**
 * Capture selected microphone channels as interleaved PCM16 at the dedicated
 * 16 kHz voice rate. This path is owned by the voice AFE worker.
 */
esp_err_t qsee_audio_capture_voice_pcm16(
    int16_t *samples,
    size_t frame_count,
    uint8_t channel_count
);

/** Reconfigure the shared speaker/microphone route for the 16 kHz voice AFE. */
esp_err_t qsee_audio_prepare_voice_route(void);

/** Close or restore the shared codec streams around global standby. */
esp_err_t qsee_audio_set_standby(bool standby);

/** Start the optional ESP-SR speech-recognition AFE worker. */
esp_err_t qsee_voice_afe_init(void);

/** Return whether the dedicated 16 kHz AFE worker owns microphone capture. */
bool qsee_voice_afe_running(void);

/** Return whether the AFE is actively consuming microphone samples. */
bool qsee_voice_afe_capture_active(void);

/** Cooperatively pause or resume the AFE capture loop. */
esp_err_t qsee_voice_afe_set_standby(bool standby);
/** Add or remove the independent Performance Mode standby reason. */
esp_err_t qsee_voice_afe_set_performance_suspended(bool suspended);
/** Yield microphone ownership while the on-device audio diagnostics are open. */
esp_err_t qsee_voice_afe_set_diagnostic_suspended(bool suspended);

/**
 * Queue already-16-kHz speaker PCM as an AEC reference. The worker interleaves
 * it with microphone channels; callers retain ownership.
 */
esp_err_t qsee_voice_afe_push_speaker_pcm16(
    const int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz
);

/** Apply the runtime UI-performance profile; the selection is session-scoped. */
esp_err_t qsee_performance_mode_set_enabled(bool enabled);
bool qsee_performance_mode_get_enabled(void);

/**
 * Start the Wi-Fi station lifecycle and BLE advertising with a
 * device-scoped name.
 */
esp_err_t qsee_radios_init(void);
esp_err_t qsee_ble_init(void);

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
typedef enum {
    QSEE_PROVISIONING_STATE_IDLE = 0,
    QSEE_PROVISIONING_STATE_STARTING,
    QSEE_PROVISIONING_STATE_WAITING_FOR_APP,
    QSEE_PROVISIONING_STATE_CREDENTIALS_RECEIVED,
    QSEE_PROVISIONING_STATE_CONNECTING,
    QSEE_PROVISIONING_STATE_SUCCESS,
    QSEE_PROVISIONING_STATE_FAILED,
    QSEE_PROVISIONING_STATE_STOPPING,
    QSEE_PROVISIONING_STATE_DEINITIALIZING,
    QSEE_PROVISIONING_STATE_STOPPED,
    QSEE_PROVISIONING_STATE_POISONED,
    QSEE_PROVISIONING_STATE_UNAVAILABLE,
} qsee_provisioning_state_t;

esp_err_t qsee_provisioning_init(void);
esp_err_t qsee_provisioning_start(void);
esp_err_t qsee_provisioning_stop(void);
/** Release provisioning BLE ownership before starting telemetry NimBLE. */
esp_err_t qsee_provisioning_deinit_for_ble(void);
/** True while the provisioning manager owns, or is changing, BLE lifecycle. */
bool qsee_provisioning_initialized(void);
bool qsee_provisioning_active(void);
qsee_provisioning_state_t qsee_provisioning_state(void);
esp_err_t qsee_provisioning_connection_summary(char *buffer, size_t buffer_size);
const char *qsee_provisioning_service_name(void);
const char *qsee_provisioning_qr_payload(void);
const char *qsee_provisioning_pop(void);

#ifdef __cplusplus
}
#endif
