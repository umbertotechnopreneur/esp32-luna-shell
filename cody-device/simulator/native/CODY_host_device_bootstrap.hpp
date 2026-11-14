#pragma once

#include <cstdint>
#include "esp_err.h"

// Each result remains NOT_FINISHED until its corresponding owner was invoked.
// Audio is optional; its backend and settings-policy results stay independent.
struct CodyHostDeviceBootstrapReport {
    esp_err_t settings_storage;
    esp_err_t initial_snapshot;
    esp_err_t pairing;
    esp_err_t panel;
    esp_err_t display;
    esp_err_t audio_backend;
};

#if defined(_WIN32)
#define CODY_HOST_BOOTSTRAP_API extern "C" __declspec(dllexport)
#else
#define CODY_HOST_BOOTSTRAP_API extern "C" __attribute__((visibility("default")))
#endif

// root: Existing absolute workspace path, encoded as UTF-8.
// settings_path: Host-only NVS file admitted by the existing path owner.
// synthetic_snapshot: Exclusive host snapshot owner, already imported by its caller.
// now_us: Original positive simulated observation time in the host monotonic domain.
// initial_brightness: Explicit simulated panel level from 0 to 100.
// panel_enabled: Explicit simulated panel gate, 0 or 1.
// reset_reason: Explicit scenario reset identity.
// report: Receives distinct actual status for each owner, including optional audio.
// Exception: Returns the original required storage, snapshot or display error.
// Audio absence never opens its policy gate and does not fabricate playback.
// Invoke once before cody_host_device_start, not alongside individual entry points.
CODY_HOST_BOOTSTRAP_API esp_err_t cody_host_device_bootstrap(const char *root, const char *settings_path,
    void *synthetic_snapshot, int64_t now_us, int initial_brightness, uint8_t panel_enabled,
    int reset_reason, CodyHostDeviceBootstrapReport *report);

// No parameters: same monotonic clock domain as the native device host adapter.
CODY_HOST_BOOTSTRAP_API int64_t cody_host_monotonic_us();

// No parameters: starts the unchanged device UI, including its settings bootstrap.
// Exception: Returns the original native startup error; no output is fabricated.
CODY_HOST_BOOTSTRAP_API esp_err_t cody_host_device_start();

// No parameters: admits the real device owner's boot-complete transition.
// Exception: Returns the native worker's exact readiness or retirement error.
CODY_HOST_BOOTSTRAP_API esp_err_t cody_host_device_ready();
