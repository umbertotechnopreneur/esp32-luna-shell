// Simulator entry point for the unmodified production settings service.
#include "LUNASHELL_settings.hpp"
#include "LUNASHELL_liveness.h"
#include "CODY_settings_service.hpp"
#include "CODY_settings.hpp"
#include <atomic>
#include "nvs.h"
#if defined(CODY_HOST_REAL_AUDIO)
#include "CODY_host_device_bootstrap.hpp"
#include "CODY_host_snapshot.hpp"
#include "LUNASHELL_platform.h"
#include "CODY_host_bsp_display.hpp"
#include "esp_timer.h"
extern "C" esp_err_t cody_host_audio_prepare();
extern "C" esp_err_t CODY_companion_start();
extern "C" esp_err_t CODY_companion_boot_completed();
#endif

#if defined(_WIN32)
#define CODY_HOST_SETTINGS_API extern "C" __declspec(dllexport)
#else
#define CODY_HOST_SETTINGS_API extern "C" __attribute__((visibility("default")))
#endif

// root: Existing absolute workspace path, encoded as UTF-8.
// settings_path: Explicit host-only .nvs file under root/artifacts/simulator.
// Exception: Returns the first original path, liveness or settings initialization error.
CODY_HOST_SETTINGS_API esp_err_t cody_host_settings_initialize(const char *root, const char *settings_path)
{
    if (!root || !settings_path) return ESP_ERR_INVALID_ARG;
    auto error = cody_host_nvs_set_root(root);
    if (error == ESP_OK) error = cody_host_nvs_set_path(settings_path);
    if (error == ESP_OK) error = LUNASHELL_liveness_init();
    if (error == ESP_OK) error = LUNASHELL::UserSettingsService::instance().initialize();
    return error;
}

// audio_output_error: Receives the separate optional audio-policy result.
// Exception: Returns the original required CODY bootstrap failure, without replacing
// a missing snapshot or storage failure with simulator defaults.
// Invoke only after the host has seeded its initial synthetic device observation.
CODY_HOST_SETTINGS_API esp_err_t cody_host_cody_settings_bootstrap(esp_err_t *audio_output_error)
{
    if (audio_output_error == nullptr) return ESP_ERR_INVALID_ARG;
    *audio_output_error = ESP_ERR_NOT_FINISHED;
    CODY::SettingsBootstrapResult result{};
    const auto error = CODY::bootstrap_device_settings(result);
    *audio_output_error = result.audio_output_error;
    return error;
}

#if defined(CODY_HOST_REAL_AUDIO)
// root: Existing absolute workspace path, encoded as UTF-8.
// settings_path: Host-only NVS file admitted by the existing path owner.
// synthetic_snapshot: Exclusive host snapshot owner containing the imported scenario.
// now_us: Positive host monotonic time in that scenario's original clock domain.
// initial_brightness: Explicit synthetic panel brightness, from 0 to 100.
// panel_enabled: Explicit synthetic panel gate, either 0 or 1.
// reset_reason: Explicit synthetic ESP reset identity.
// report: Receives each stage result, with optional audio errors kept distinct.
// Exception: Returns the original required storage, snapshot or display error.
CODY_HOST_SETTINGS_API esp_err_t cody_host_device_bootstrap(const char *root, const char *settings_path,
    void *synthetic_snapshot, int64_t now_us, int initial_brightness, uint8_t panel_enabled,
    int reset_reason, CodyHostDeviceBootstrapReport *report)
{
    if (!root || !settings_path || !synthetic_snapshot || now_us <= 0 || !report ||
        initial_brightness < 0 || initial_brightness > 100 || panel_enabled > 1 ||
        reset_reason < 0 || reset_reason > 10) return ESP_ERR_INVALID_ARG;
    *report = {ESP_ERR_NOT_FINISHED, ESP_ERR_NOT_FINISHED, ESP_ERR_NOT_FINISHED, ESP_ERR_NOT_FINISHED,
        ESP_ERR_NOT_FINISHED, ESP_ERR_NOT_FINISHED};
    report->settings_storage = cody_host_settings_initialize(root, settings_path);
    if (report->settings_storage != ESP_OK) return report->settings_storage;
    report->initial_snapshot = cody_host_snapshot_publish_onboarding(synthetic_snapshot, now_us);
    if (report->initial_snapshot != ESP_OK) return report->initial_snapshot;
    // Firmware starts the pairing worker before starting the Cody UI.
    report->pairing = LUNASHELL_pairing_service_init();
    if (report->pairing != ESP_OK) return report->pairing;
    report->panel = cody_host_bsp_display_initialize(initial_brightness, panel_enabled != 0, reset_reason);
    if (report->panel != ESP_OK) return report->panel;
    report->display = LUNASHELL_display_runtime_init();
    if (report->display != ESP_OK) return report->display;
    // An unavailable host device is optional. The real companion start will
    // initialize its own settings exactly once and retain any audio failure.
    report->audio_backend = cody_host_audio_prepare();
    return ESP_OK;
}

// No parameters: reads the same monotonic host clock used by the device adapters.
// Returns zero only during its first microsecond of initialization.
CODY_HOST_SETTINGS_API int64_t cody_host_monotonic_us() { return esp_timer_get_time(); }

// No parameters: select the static P04 clock before snapshot/bootstrap/workers exist.
// Exception: Repeated activation or activation after bootstrap is rejected.
CODY_HOST_SETTINGS_API esp_err_t cody_host_studio_clock_initialize()
{
    if (LUNASHELL::UserSettingsService::instance().isInitialized()) return ESP_ERR_INVALID_STATE;
    int64_t expected = 0;
    return cody_host_manual_time_us.compare_exchange_strong(expected, 1000000,
        std::memory_order_acq_rel) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

namespace {
std::atomic<bool> s_studio_boot_seeded{false};
std::atomic<bool> s_host_device_start_attempted{false};
}

// No parameters: seed an isolated boot replay before the firmware owner starts.
// Exception: Preserves each verified settings-owner write/readback failure.
CODY_HOST_SETTINGS_API esp_err_t cody_host_studio_seed_boot_replay()
{
    if (!LUNASHELL::UserSettingsService::instance().isInitialized() ||
        cody_host_manual_time_us.load(std::memory_order_acquire) != 1000000 ||
        s_host_device_start_attempted.load(std::memory_order_acquire)) return ESP_ERR_INVALID_STATE;
    bool expected = false;
    if (!s_studio_boot_seeded.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return ESP_ERR_INVALID_STATE;
    auto error = CODY::persist_first_boot(false);
    if (error == ESP_OK) error = CODY::persist_birth_intro_pending(true);
    if (error == ESP_OK) error = CODY::persist_first_boot_music_pending(false);
    return error;
}

// No parameters: confirm the boot owner's loaded storage state after start.
// Exception: Rejects forced first boot and any unconfirmed or inconsistent state.
CODY_HOST_SETTINGS_API esp_err_t cody_host_studio_verify_boot_replay()
{
    if (!s_studio_boot_seeded.load(std::memory_order_acquire) ||
        !s_host_device_start_attempted.load(std::memory_order_acquire)) return ESP_ERR_INVALID_STATE;
    CODY::SettingsModel model{};
    const auto error = CODY::read_settings_model(model);
    if (error != ESP_OK) return error;
    const auto &onboarding = model.onboarding;
    const auto &storage = model.storage;
    if (!storage.first_boot.valid || storage.first_boot.error != ESP_OK ||
        !storage.birth_intro.valid || storage.birth_intro.error != ESP_OK ||
        !storage.first_boot_music.valid || storage.first_boot_music.error != ESP_OK ||
        onboarding.persisted_first_boot || onboarding.effective_first_boot || onboarding.force_first_boot ||
        !onboarding.birth_intro_pending || onboarding.first_boot_music_pending) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

// No parameters: starts the real device owner and its own settings bootstrap.
// Exception: Propagates the original startup failure without synthesizing a frame.
CODY_HOST_SETTINGS_API esp_err_t cody_host_device_start()
{
    s_host_device_start_attempted.store(true, std::memory_order_release);
    return CODY_companion_start();
}

// No parameters: releases the original UI worker's boot-complete handoff.
// Exception: Propagates native readiness and completed-frame errors unchanged.
CODY_HOST_SETTINGS_API esp_err_t cody_host_device_ready()
{
    return CODY_companion_boot_completed();
}
#endif
