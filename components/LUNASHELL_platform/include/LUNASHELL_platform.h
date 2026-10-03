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
} LUNASHELL_calendar_time_t;

/**
 * Register the allocation-failure monitor before other firmware services.
 *
 * Registration is idempotent. A registration failure is returned unchanged so
 * startup code can stop before any unobserved allocation-heavy initialization.
 */
esp_err_t LUNASHELL_memory_telemetry_init(void);

/**
 * Probe and initialize hardware attached to the Waveshare BSP I2C bus.
 *
 * Returns the original initialization error from the first failing peripheral.
 * Any decision to continue after an optional-device failure belongs to the
 * caller; this platform layer never converts a partial failure into success.
 */
esp_err_t LUNASHELL_hardware_init(void);

/** Refresh the canonical hardware snapshot once. */
esp_err_t LUNASHELL_hardware_refresh(const char *reason);

/**
 * Atomically consume AXP2101 PKEY events captured from the PMIC IRQ status.
 *
 * The hardware service owns the IRQ registers and clears them only after the
 * decoded button events have been queued for the power manager.
 */
esp_err_t LUNASHELL_hardware_take_power_key_events(
    uint32_t *short_presses,
    uint32_t *long_presses
);

/**
 * Start or stop the periodic Full Snapshot worker.
 *
 * The worker serializes complete hardware refreshes and publishes every result
 * through DeviceSnapshotService. It is independent from diagnostic sampling.
 */
esp_err_t LUNASHELL_hardware_start_full_snapshot_monitor(void);
esp_err_t LUNASHELL_hardware_stop_full_snapshot_monitor(void);
/** Suppress periodic Full Snapshots without changing the requested lifecycle. */
esp_err_t LUNASHELL_hardware_set_full_snapshot_performance_suspended(bool suspended);
/** Quiesce Full Snapshots for system standby without changing app lifecycle. */
esp_err_t LUNASHELL_hardware_set_full_snapshot_power_suspended(bool suspended);
/** Suspend or restore the LC76G GNSS DSP/RF subsystem through its I2C bridge. */
esp_err_t LUNASHELL_hardware_set_gps_power_suspended(bool suspended);
/** Persist one supported Full Snapshot cadence: 300-10000 ms presets only. */
esp_err_t LUNASHELL_hardware_set_full_snapshot_period_ms(uint32_t period_ms);

/**
 * Select the measured streams needed by the active diagnostic page.
 *
 * IMU and microphone sampling are independently gated so an audio page never
 * polls the IMU and the IMU page never claims the microphone. Spectrum work is
 * a separate lease so level-only pages do not execute the DFT bank.
 */
esp_err_t LUNASHELL_hardware_set_diagnostic_streams(
    bool imu_enabled,
    bool microphone_enabled,
    bool microphone_spectrum_enabled
);
/**
 * Enable or disable lightweight IMU publication for adaptive UI wake.
 *
 * The monitor publishes only measured IMU samples through DeviceSnapshotService;
 * it does not poll microphones or expose a second sensor-data path to the UI.
 */
esp_err_t LUNASHELL_hardware_set_ui_motion_monitor(bool enabled);
/**
 * Retain qualified IMU motion wake at a low 1 Hz host sampling cadence while
 * Device Standby is active. The normal 10 Hz cadence is restored on wake.
 */
esp_err_t LUNASHELL_hardware_set_ui_motion_monitor_standby(bool standby);
/**
 * Quiesce optional diagnostic streams and auto-rotation during Device Standby
 * without changing their requested settings. Low-rate UI motion wake remains
 * enabled independently and a fresh orientation sample is required on wake.
 */
esp_err_t LUNASHELL_hardware_set_ui_sampling_power_suspended(bool suspended);
/**
 * Select the QMI8658 active or software-standby sampling profile.
 *
 * The low-power profile keeps accelerometer and gyroscope measurements valid
 * at their lowest common 31.25 Hz ODR. Disabling it restores the normal 8
 * g/500 Hz accelerometer and 512 dps/500 Hz gyroscope configuration.
 */
esp_err_t LUNASHELL_hardware_set_imu_low_power(bool enabled);
/** Configure the acquisition owner's accelerometer-only INT2 wake profile. */
esp_err_t LUNASHELL_hardware_set_imu_motion_wakeup(bool enabled);
/** Configure the active diagnostic sample period (100-2000 ms). */
esp_err_t LUNASHELL_hardware_set_diagnostic_period_ms(uint32_t period_ms);
/** Return the current diagnostic sample period in milliseconds. */
uint32_t LUNASHELL_hardware_get_diagnostic_period_ms(void);
/** Configure low-pass smoothing of calibrated IMU input (0-90 percent). */
esp_err_t LUNASHELL_hardware_set_imu_smoothing_percent(uint32_t percent);
/** Return the persisted IMU input smoothing percentage. */
uint32_t LUNASHELL_hardware_get_imu_smoothing_percent(void);

/**
 * Schedule an explicit stationary QMI8658 calibration on the platform worker.
 *
 * The request rejects moving sample sets, keeps the previous valid calibration,
 * and returns immediately once the worker accepts the calibration.
 */
esp_err_t LUNASHELL_imu_calibrate(void);

/**
 * Start one asynchronous robust calibration of the two TOF400F modules.
 *
 * The current persisted sample count, interval, and physical reference are
 * captured when the worker accepts the request.
 */
esp_err_t LUNASHELL_distance_start_calibration(void);
/** Persist 20..500 accepted samples per robust distance window. */
esp_err_t LUNASHELL_distance_set_sample_count(uint16_t sample_count);
/** Persist one supported TOF interval: 250, 500, or 1000 milliseconds. */
esp_err_t LUNASHELL_distance_set_sample_interval_ms(uint32_t interval_ms);
/** Persist the 500..1500 mm physical calibration reference, in 10 mm steps. */
esp_err_t LUNASHELL_distance_set_reference_mm(uint16_t reference_mm);
/** Quiesce distance acquisition and its UART transports for system standby. */
esp_err_t LUNASHELL_distance_set_power_suspended(bool suspended);

/** Read or write the board PCF85063. The RTC is maintained in UTC. */
esp_err_t LUNASHELL_rtc_read(LUNASHELL_calendar_time_t *time);
esp_err_t LUNASHELL_rtc_write(const LUNASHELL_calendar_time_t *time);

/** Restore system time from RTC and prepare deferred SNTP synchronization. */
esp_err_t LUNASHELL_time_init(const char *timezone);

/** Start SNTP after DHCP has supplied an IP address. */
esp_err_t LUNASHELL_time_notify_wifi_online(void);

/** Apply a valid UTC GPS date/time to system time and the RTC. */
esp_err_t LUNASHELL_time_sync_from_gps(
    uint16_t year,
    uint8_t month,
    uint8_t day,
    uint8_t hour,
    uint8_t minute,
    uint8_t second
);

typedef enum {
    LUNASHELL_POWER_TIMEOUT_COMMAND_ARC = 0,
    LUNASHELL_POWER_TIMEOUT_STATUS_ARC = 1,
    LUNASHELL_POWER_TIMEOUT_DISPLAY_DIM = 2,
    LUNASHELL_POWER_TIMEOUT_SCREEN_SLEEP = 3,
    LUNASHELL_POWER_TIMEOUT_SCREEN_IDLE = 4,
} LUNASHELL_power_timeout_t;

/** Start the single BOOT/PWR, inactivity, display, audio, and IMU service. */
esp_err_t LUNASHELL_power_manager_init(void);

// sleeping: Prepare a complete visible sleep frame, or the first wake frame while dark.
// The callback owns its renderer synchronization and returns the original failure.
typedef esp_err_t (*LUNASHELL_display_lifecycle_callback_t)(bool sleeping);
// callback: Single display consumer; register once before power-manager startup.
esp_err_t LUNASHELL_power_manager_register_display_lifecycle(
    LUNASHELL_display_lifecycle_callback_t callback
);

/** Record activity without blocking the touch or UI callback. */
void LUNASHELL_power_manager_note_activity(void);

/** Select Leg Edition's motion/audio behavior; the shared idle timeout still applies. */
esp_err_t LUNASHELL_power_manager_set_leg_mode(bool active, bool wake_on_motion);
/** Enable or silence the movement cues without changing the screen policy. */
esp_err_t LUNASHELL_power_manager_set_leg_sounds_enabled(bool enabled);

/** Publish the exact UI quiescence result for one standby generation. */
void LUNASHELL_power_manager_note_ui_standby_result(
    uint32_t transition_sequence,
    esp_err_t result
);

/** Persist and apply one supported inactivity timeout. */
esp_err_t LUNASHELL_power_manager_set_timeout(
    LUNASHELL_power_timeout_t timeout,
    uint32_t seconds
);

/** Read one active inactivity timeout from the power service. */
esp_err_t LUNASHELL_power_manager_get_timeout(
    LUNASHELL_power_timeout_t timeout,
    uint32_t *seconds
);

/** Explicit Light-sleep callers own UI lifecycle; Deep-sleep requires Release
 * and confirmed transactional standby on the power-owner task. */
esp_err_t LUNASHELL_power_enter_light_sleep(uint64_t wake_after_us);
esp_err_t LUNASHELL_power_enter_deep_sleep(uint64_t wake_after_us);
/** Release retained wake pins before any peripheral initialization. */
esp_err_t LUNASHELL_power_prepare_boot(void);
/** Fade in after the first valid frame following a Deep-sleep restart. */
esp_err_t LUNASHELL_power_restore_wake_display(void);

/** Start non-blocking SD detection/mount retries. Never formats automatically. */
esp_err_t LUNASHELL_storage_init(void);

/**
 * Arm one immediate format attempt after explicit user authorization.
 *
 * The authorization is accepted only for the exact card identity from the
 * latest verified FR_NO_FILESYSTEM probe and expires when consumed.
 */
esp_err_t LUNASHELL_storage_request_format(void);
/** Ask the storage owner task for one fresh mounted-filesystem space sample. */
esp_err_t LUNASHELL_storage_request_space_refresh(void);
esp_err_t LUNASHELL_storage_set_logger_mirror(bool enabled);
/** Pause effective SD log writes while preserving the user's saved preference. */
esp_err_t LUNASHELL_storage_set_performance_suspended(bool suspended);
/**
 * Quiesce SD polling and mirroring for standby without unmounting the card.
 * Resume validates the mounted card and FAT before restoring user mirroring.
 */
esp_err_t LUNASHELL_storage_set_power_suspended(bool suspended);

/** Initialize the standalone display and touch drivers without the UI backend. */
esp_err_t LUNASHELL_display_runtime_init(void);

/** Drain standalone rectangle writes and gate further writes during standby. */
// suspended: Whether standalone rectangle writes must remain blocked.
esp_err_t LUNASHELL_display_runtime_set_suspended(bool suspended);

/** Query initialized standalone write availability without copying a full snapshot. */
bool LUNASHELL_display_runtime_writes_allowed(void);

/** Query the panel revision; a change requires the caller to redraw its full content. */
uint32_t LUNASHELL_display_runtime_content_revision(void);

/** Begin one bounded standalone frame; the same task must finish it. */
// budget_ms: Aggregate rendering/transfer budget, 1..1000 ms after acquiring the lock.
esp_err_t LUNASHELL_display_frame_begin(uint32_t budget_ms);
/** Check the frame deadline between geometry/raster stages, without resetting it. */
esp_err_t LUNASHELL_display_frame_check_deadline(void);
// pixels: Receives idle internal raster storage borrowed only until this frame ends.
// capacity_bytes: Actual usable capacity; never an assumed preferred tile size.
esp_err_t LUNASHELL_display_frame_buffer(void **pixels, size_t *capacity_bytes);
// frame_error: First geometry/raster/transfer error; partial frames cannot report success.
esp_err_t LUNASHELL_display_frame_end(esp_err_t frame_error);

typedef enum {
    LUNASHELL_DISPLAY_RGB565_BYTE_ORDER_NATIVE = 0,
    LUNASHELL_DISPLAY_RGB565_BYTE_ORDER_PANEL = 1,
} LUNASHELL_display_rgb565_byte_order_t;

/** Write one explicitly ordered packed RGB565 rectangle through the standalone panel. */
// x_start: First included horizontal pixel coordinate.
// y_start: First included vertical pixel coordinate.
// x_end: First excluded horizontal pixel coordinate.
// y_end: First excluded vertical pixel coordinate.
// pixels: Packed RGB565 pixels retained until the synchronous write returns.
// byte_order: Explicit source representation mapped to the BSP without guessing.
esp_err_t LUNASHELL_display_write_rgb565(
    int x_start,
    int y_start,
    int x_end,
    int y_end,
    const void *pixels,
    LUNASHELL_display_rgb565_byte_order_t byte_order
);

typedef struct {
    bool valid;
    bool pressed;
    uint16_t x;
    uint16_t y;
    esp_err_t error;
    int64_t sample_timestamp_us;
} LUNASHELL_touch_point_t;

/** Read one IRQ-triggered report. NOT_FOUND with timestamp zero means no new sample. */
// point: Receives the real touch state or an explicit invalid state and error.
esp_err_t LUNASHELL_touch_read_point(LUNASHELL_touch_point_t *point);

// timeout_ms: Maximum idle wait; NOT_FOUND means no IRQ arrived before the deadline.
esp_err_t LUNASHELL_touch_wait_for_report(uint32_t timeout_ms);

/** Persist and apply AMOLED brightness (10-100 percent). */
esp_err_t LUNASHELL_display_set_brightness_percent(uint32_t percent);
esp_err_t LUNASHELL_display_get_brightness_percent(uint32_t *percent);
esp_err_t LUNASHELL_display_apply_saved_brightness(void);
/** Enable frame writes at zero brightness without changing the saved brightness.
 * The caller must render the wake frame before fading to the active power mode. */
esp_err_t LUNASHELL_display_prepare_dark_frame(void);

/** Persist the automatic cardinal-rotation preference. */
esp_err_t LUNASHELL_display_set_auto_rotation_enabled(bool enabled);

/** Persist and apply a manual cardinal rotation while automatic rotation is disabled. */
// degrees: Exact panel and touch rotation, restricted to 0, 90, 180, or 270 degrees.
esp_err_t LUNASHELL_display_set_rotation_degrees(uint32_t degrees);

/** Return the active automatic cardinal-rotation preference. */
bool LUNASHELL_display_get_auto_rotation_enabled(void);

/** Add a true 180-degree panel/touch rotation after automatic orientation. */
esp_err_t LUNASHELL_display_set_leg_flip_enabled(bool enabled);
bool LUNASHELL_display_get_leg_flip_enabled(void);

/** Restore the persisted preference and last successfully applied rotation. */
esp_err_t LUNASHELL_display_apply_saved_auto_rotation(void);

typedef enum {
    LUNASHELL_DISPLAY_POWER_ACTIVE = 0,
    LUNASHELL_DISPLAY_POWER_DIMMED = 1,
    LUNASHELL_DISPLAY_POWER_OFF = 2,
} LUNASHELL_display_power_mode_t;

/** Apply a transient display power mode without changing persisted brightness. */
esp_err_t LUNASHELL_display_set_power_mode(LUNASHELL_display_power_mode_t mode);

/** Fade to a transient display power mode without changing saved brightness. */
esp_err_t LUNASHELL_display_fade_to_power_mode(
    LUNASHELL_display_power_mode_t mode,
    uint32_t duration_ms
);

/**
 * Report the result of an actual speaker playback attempt.
 */
void LUNASHELL_hardware_report_speaker(bool ready, esp_err_t error);

/** Initialize the selected speaker and microphone codec paths. */
esp_err_t LUNASHELL_audio_init(void);

// samples: Immutable PCM16 in internal flash, retained until the one-time startup cue completes.
// sample_count: Exact mono sample count, not a byte count.
// sample_rate_hz: Codec-compatible startup format.
// volume_percent: Explicit 0..100 output volume.
esp_err_t LUNASHELL_audio_set_startup_cue(const int16_t *samples, size_t sample_count,
                                        uint32_t sample_rate_hz, int volume_percent);

/** Return the number of physically selected microphone channels: 0, 1, or 2. */
uint8_t LUNASHELL_audio_input_channel_count(void);

/** Play mono signed PCM16 through the board speaker. */
esp_err_t LUNASHELL_audio_play_pcm16(
    const int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz,
    int volume_percent
);

/** Capture mono signed PCM16 when the selected board exposes one microphone. */
esp_err_t LUNASHELL_audio_capture_pcm16(
    int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz,
    float gain_db
);

/** Capture interleaved PCM16 frames from the ES7210 microphone channels. */
esp_err_t LUNASHELL_audio_capture_pcm16_channels(
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
esp_err_t LUNASHELL_audio_capture_voice_pcm16(
    int16_t *samples,
    size_t frame_count,
    uint8_t channel_count
);

/** Reconfigure the shared speaker/microphone route for the 16 kHz voice AFE. */
esp_err_t LUNASHELL_audio_prepare_voice_route(void);

/** Atomically suspend or restore shared codec streams and the I2S transport. */
esp_err_t LUNASHELL_audio_set_standby(bool standby);

/** Start the optional ESP-SR speech-recognition AFE worker. */
esp_err_t LUNASHELL_voice_afe_init(void);

/** Return whether the dedicated 16 kHz AFE worker owns microphone capture. */
bool LUNASHELL_voice_afe_running(void);

/** Return whether the AFE is actively consuming microphone samples. */
bool LUNASHELL_voice_afe_capture_active(void);

/** Cooperatively pause or resume the AFE capture loop. */
esp_err_t LUNASHELL_voice_afe_set_standby(bool standby);
/** Add or remove the independent Performance Mode standby reason. */
esp_err_t LUNASHELL_voice_afe_set_performance_suspended(bool suspended);
/** Yield microphone ownership while the on-device audio diagnostics are open. */
esp_err_t LUNASHELL_voice_afe_set_diagnostic_suspended(bool suspended);

/**
 * Queue already-16-kHz speaker PCM as an AEC reference. The worker interleaves
 * it with microphone channels; callers retain ownership.
 */
esp_err_t LUNASHELL_voice_afe_push_speaker_pcm16(
    const int16_t *samples,
    size_t sample_count,
    uint32_t sample_rate_hz
);

typedef struct {
    /** Last completely committed target; authoritative only when state_known. */
    bool enabled;
    /** False after any ambiguous component transition until full reconciliation. */
    bool state_known;
    /** Error returned by the component that made the state ambiguous. */
    esp_err_t primary_error;
    /** First coordinator rollback error, retained until reconciliation succeeds. */
    esp_err_t rollback_error;
} LUNASHELL_performance_mode_status_t;

/** Apply the runtime UI-performance profile; the selection is session-scoped. */
esp_err_t LUNASHELL_performance_mode_set_enabled(bool enabled);
bool LUNASHELL_performance_mode_get_enabled(void);
/** Report whether every Performance Mode owner agrees with the committed state. */
void LUNASHELL_performance_mode_get_status(
    LUNASHELL_performance_mode_status_t *status
);

/**
 * Start the Wi-Fi station lifecycle and BLE advertising with a
 * device-scoped name.
 */
esp_err_t LUNASHELL_radios_init(void);
esp_err_t LUNASHELL_radios_stop(void);
esp_err_t LUNASHELL_ble_init(void);
/** Explicit telemetry-to-provisioning handoff; call from a service worker. */
esp_err_t LUNASHELL_radios_start_pairing(void);
/** Close provisioning before optionally restoring telemetry on verified Wi-Fi. */
esp_err_t LUNASHELL_radios_end_pairing(bool restore_telemetry);

#define LUNASHELL_WIFI_MAX_NETWORKS 16
#define LUNASHELL_WIFI_SSID_MAX_LEN 32

typedef struct {
    char ssid[LUNASHELL_WIFI_SSID_MAX_LEN + 1];
    int8_t rssi;
    uint8_t channel;
    bool secured;
} LUNASHELL_wifi_network_t;

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
    char ssid[LUNASHELL_WIFI_SSID_MAX_LEN + 1];
    char ip_address[16];
    char netmask[16];
    char gateway[16];
    char mac_address[18];
} LUNASHELL_wifi_status_t;

/**
 * Initialize one event-driven ESP-IDF station lifecycle.
 *
 * A profile previously stored by ESP-IDF is connected automatically.
 */
esp_err_t LUNASHELL_wifi_init(void);
esp_err_t LUNASHELL_wifi_stop(void);

/**
 * Apply a station profile and connect. The password is never written to logs.
 *
 * When persist is true ESP-IDF stores the profile in NVS.
 */
esp_err_t LUNASHELL_wifi_connect(const char *ssid, const char *password, bool persist);

/**
 * Stop reconnect attempts while leaving the station driver available.
 */
esp_err_t LUNASHELL_wifi_disconnect(void);

/**
 * Start one asynchronous all-channel scan.
 */
esp_err_t LUNASHELL_wifi_start_scan(void);

/**
 * Copy fixed scan results and the immutable connection state.
 */
size_t LUNASHELL_wifi_get_networks(LUNASHELL_wifi_network_t *networks, size_t capacity);
void LUNASHELL_wifi_get_status(LUNASHELL_wifi_status_t *status);

/** Start/stop secure stock ESP-IDF BLE Wi-Fi provisioning. */
typedef enum {
    LUNASHELL_PROVISIONING_STATE_IDLE = 0,
    LUNASHELL_PROVISIONING_STATE_STARTING,
    LUNASHELL_PROVISIONING_STATE_WAITING_FOR_APP,
    LUNASHELL_PROVISIONING_STATE_CREDENTIALS_RECEIVED,
    LUNASHELL_PROVISIONING_STATE_CONNECTING,
    LUNASHELL_PROVISIONING_STATE_SUCCESS,
    LUNASHELL_PROVISIONING_STATE_FAILED,
    LUNASHELL_PROVISIONING_STATE_STOPPING,
    LUNASHELL_PROVISIONING_STATE_DEINITIALIZING,
    LUNASHELL_PROVISIONING_STATE_STOPPED,
    LUNASHELL_PROVISIONING_STATE_POISONED,
    LUNASHELL_PROVISIONING_STATE_UNAVAILABLE,
} LUNASHELL_provisioning_state_t;

typedef enum {
    LUNASHELL_PROVISIONING_FAILURE_NONE = 0,
    LUNASHELL_PROVISIONING_FAILURE_AUTHENTICATION,
    LUNASHELL_PROVISIONING_FAILURE_ACCESS_POINT_NOT_FOUND,
    LUNASHELL_PROVISIONING_FAILURE_UNKNOWN,
} LUNASHELL_provisioning_failure_reason_t;

esp_err_t LUNASHELL_provisioning_init(void);
esp_err_t LUNASHELL_provisioning_start(void);
esp_err_t LUNASHELL_provisioning_stop(void);
/** Release provisioning BLE ownership before starting telemetry NimBLE. */
esp_err_t LUNASHELL_provisioning_deinit_for_ble(void);
/** True while the provisioning manager owns, or is changing, BLE lifecycle. */
bool LUNASHELL_provisioning_initialized(void);
bool LUNASHELL_provisioning_active(void);
LUNASHELL_provisioning_state_t LUNASHELL_provisioning_state(void);
LUNASHELL_provisioning_failure_reason_t LUNASHELL_provisioning_failure_reason(void);
esp_err_t LUNASHELL_provisioning_connection_summary(char *buffer, size_t buffer_size);
const char *LUNASHELL_provisioning_service_name(void);
const char *LUNASHELL_provisioning_qr_payload(void);
const char *LUNASHELL_provisioning_pop(void);
/** Copy the active pairing QR without returning shared mutable storage. */
esp_err_t LUNASHELL_provisioning_copy_qr(char *buffer, size_t buffer_size, uint32_t session_id);

typedef enum {
    LUNASHELL_PAIRING_ACTION_START,
    LUNASHELL_PAIRING_ACTION_RETRY,
    LUNASHELL_PAIRING_ACTION_CANCEL,
} LUNASHELL_pairing_action_t;

/** Initialize the internal-stack async pairing controller; never opens BLE. */
esp_err_t LUNASHELL_pairing_service_init(void);
/** Submit one explicit action against the currently observed attempt identity. */
esp_err_t LUNASHELL_pairing_request(LUNASHELL_pairing_action_t action, uint32_t expected_attempt_id);

#ifdef __cplusplus
}
#endif
