/* VBWR B
 *
 * Project: ESP32 Luna Shell
 * Repository: https://github.com/umbertotechnopreneur/esp32-luna-shell
 * Creator: Umberto Giacobbi | https://umbertogiacobbi.biz
 *
 * VibeWare: Human intent, AI execution, and plenty of tokens
 * Manifesto: https://umbertogiacobbi.biz/vibeware/manifesto
 *
 * Modified with AI: OpenAI Codex; added VibeWare branding on 2026-10-10.
 * Human guidance: Umberto Giacobbi; requested VibeWare branding.
 *
 * Copyright (c) 2026 Umberto Giacobbi
 *
 * VBWR E */

/* Physical Deep-sleep wake wiring for the GPS Waveshare 1.75-inch board. */
#include "LUNASHELL_sleep.hpp"
#include "LUNASHELL_platform.h"
#include "LUNASHELL_build_mode.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "LUNASHELL_power_policy.hpp"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"

namespace {

constexpr char kTag[] = "LUNASHELL_sleep";
bool s_recovery_complete = true;
constexpr gpio_num_t kTouchInterrupt = GPIO_NUM_11;
constexpr gpio_num_t kImuInterrupt2 = GPIO_NUM_21;
struct RetainedOutput {
    gpio_num_t pin;
    uint32_t level;
};
constexpr RetainedOutput kRetainedOutputs[] = {
    {GPIO_NUM_12, 1},  // QSPI chip select stays inactive while its host is powered off.
    {GPIO_NUM_39, 1},  // Panel reset stays deasserted while the MCU sleeps.
    {GPIO_NUM_40, 1},  // Touch must remain powered and out of reset to detect a finger.
    {GPIO_NUM_46, 0},  // The physical NS4150B amplifier remains shut down.
};

// No parameters: restore digital ownership before the BSP initializes peripherals.
esp_err_t release_retained_outputs()
{
    for (const auto &output : kRetainedOutputs) {
        // Set the desired post-hold state first; otherwise reset/amp pins can glitch.
        ESP_RETURN_ON_ERROR(gpio_set_level(output.pin, output.level), kTag, "Post-sleep GPIO%d level", output.pin);
        ESP_RETURN_ON_ERROR(gpio_set_direction(output.pin, GPIO_MODE_OUTPUT), kTag, "Post-sleep GPIO%d direction", output.pin);
        ESP_RETURN_ON_ERROR(gpio_hold_dis(output.pin), kTag, "Post-sleep GPIO%d hold release", output.pin);
    }
    gpio_deep_sleep_hold_dis();
    return ESP_OK;
}

// No parameters: release both the RTC hold and mux before digital touch IRQs resume.
esp_err_t release_wake_inputs()
{
    ESP_RETURN_ON_ERROR(rtc_gpio_hold_dis(kTouchInterrupt), kTag, "Release touch RTC hold");
    ESP_RETURN_ON_ERROR(rtc_gpio_deinit(kTouchInterrupt), kTag, "Release touch RTC pad");
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU
    ESP_RETURN_ON_ERROR(rtc_gpio_hold_dis(kImuInterrupt2), kTag, "Release IMU RTC hold");
    ESP_RETURN_ON_ERROR(rtc_gpio_deinit(kImuInterrupt2), kTag, "Release IMU RTC pad");
#endif
    return ESP_OK;
}

// No parameters: hold the board's reset and amplifier outputs across Deep-sleep.
esp_err_t retain_sleep_outputs()
{
    for (const auto &output : kRetainedOutputs) {
        ESP_RETURN_ON_ERROR(gpio_set_level(output.pin, output.level), kTag, "Sleep GPIO%d level", output.pin);
        ESP_RETURN_ON_ERROR(gpio_set_direction(output.pin, GPIO_MODE_OUTPUT), kTag, "Sleep GPIO%d direction", output.pin);
        ESP_RETURN_ON_ERROR(gpio_hold_en(output.pin), kTag, "Sleep GPIO%d hold", output.pin);
    }
    gpio_deep_sleep_hold_en();
    return ESP_OK;
}

// wake_after_us: Optional timer; normal Release inactivity passes zero.
// Exception: Returns original canonical readiness, GPIO or sleep-source configuration errors.
esp_err_t prepare_sleep(uint64_t wake_after_us)
{
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
    // Touch readiness does not require copying audio, inventories or growing CCP schemas
    // onto the power task's nested sleep call chain.
    LUNASHELL::ComponentSnapshot touch;
    ESP_RETURN_ON_ERROR(LUNASHELL::DeviceSnapshotService::instance()
        .readTouchHealthObservation(touch), kTag, "Read canonical touch wake readiness");
    if (touch.health != LUNASHELL::DeviceHealth::Ready) {
        ESP_LOGE(kTag, "Deep-sleep rejected: touch wake unavailable (%s)",
                 esp_err_to_name(touch.last_error));
        return touch.last_error != ESP_OK ? touch.last_error : ESP_ERR_INVALID_STATE;
    }
    uint64_t wake_pins = 1ULL << kTouchInterrupt;
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU
    ESP_RETURN_ON_ERROR(gpio_set_direction(kImuInterrupt2, GPIO_MODE_INPUT), kTag, "Configure IMU wake input");
    ESP_RETURN_ON_ERROR(LUNASHELL_hardware_set_imu_motion_wakeup(true), kTag, "Arm QMI8658 INT2 wake");
    wake_pins |= 1ULL << kImuInterrupt2;
#endif
    // This is a wake-level check, not a second touch/IMU measurement path.
    // Refuse an asserted IRQ instead of repeatedly rebooting while a finger is held.
    if (gpio_get_level(kTouchInterrupt) == 0
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU
        || gpio_get_level(kImuInterrupt2) == 0
#endif
    ) {
        ESP_LOGW(kTag, "Deep-sleep rejected: touch or motion IRQ is already active");
        return ESP_ERR_SLEEP_REJECT;
    }
    ESP_RETURN_ON_ERROR(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL), kTag, "Clear wake sources");
    ESP_RETURN_ON_ERROR(esp_sleep_disable_ext1_wakeup_io(0), kTag, "Clear EXT1 wake pins");
    ESP_RETURN_ON_ERROR(esp_sleep_enable_ext1_wakeup_io(wake_pins, ESP_EXT1_WAKEUP_ANY_LOW), kTag, "Arm touch/motion EXT1");
    if (wake_after_us > 0) {
        ESP_RETURN_ON_ERROR(esp_sleep_enable_timer_wakeup(wake_after_us), kTag, "Arm explicit wake timer");
    }
    // No PMU/PWR or periodic polling wake is armed. Keep the common 3.3-V rail on.
    return retain_sleep_outputs();
#else
    (void)wake_after_us;
    ESP_LOGE(kTag, "Deep-sleep rejected: this profile excludes physical touch wake");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

}  // namespace

// No parameters: run before any BSP/IMU initialization on a Deep-sleep restart.
extern "C" esp_err_t LUNASHELL_power_prepare_boot(void)
{
    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(release_retained_outputs(), kTag, "Release board sleep outputs");
    ESP_RETURN_ON_ERROR(release_wake_inputs(), kTag, "Release board RTC wake inputs");
    ESP_LOGW(kTag, "Deep-sleep restart: wake pins=0x%llx; normal startup restores services",
             static_cast<unsigned long long>(esp_sleep_get_ext1_wakeup_status()));
    return ESP_OK;
}

// No parameters: expose the first rendered wake frame with the shared 800-ms fade.
extern "C" esp_err_t LUNASHELL_power_restore_wake_display(void)
{
    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        return ESP_OK;
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    return LUNASHELL_display_fade_to_power_mode(
        LUNASHELL_DISPLAY_POWER_ACTIVE, LUNASHELL::power_policy::kDisplayFadeDurationMs);
#else
    return ESP_OK;
#endif
}

namespace LUNASHELL::sleep {

// wake_after_us: Optional explicit timer; normal inactivity uses only touch/IMU wake.
esp_err_t try_deep_sleep(uint64_t wake_after_us)
{
    if constexpr (!build_mode::kDeepSleepOnStandbyEnabled) {
        ESP_LOGE(kTag, "Deep-sleep rejected in Debug; CPU and USB remain available");
        return ESP_ERR_NOT_ALLOWED;
    }
    s_recovery_complete = false;
    esp_err_t result = prepare_sleep(wake_after_us);
    if (result == ESP_OK) {
        result = esp_deep_sleep_try_to_start();
    }
    if (result == ESP_OK) {
        result = ESP_ERR_INVALID_STATE;
    }
    // Successful Deep-sleep never returns. Any return must preserve the real error
    // and remove holds/WoM before the power transaction resumes ordinary sampling.
    ESP_LOGE(kTag, "Deep-sleep entry failed (%s)", esp_err_to_name(result));
    const esp_err_t release_error = release_retained_outputs();
    if (release_error != ESP_OK) {
        ESP_LOGE(kTag, "Deep-sleep GPIO recovery failed (%s)", esp_err_to_name(release_error));
    }
    const esp_err_t wake_error = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (wake_error != ESP_OK) {
        ESP_LOGE(kTag, "Deep-sleep wake-source recovery failed (%s)", esp_err_to_name(wake_error));
    }
    const esp_err_t input_error = release_wake_inputs();
    if (input_error != ESP_OK) {
        ESP_LOGE(kTag, "Deep-sleep wake-pin recovery failed (%s)", esp_err_to_name(input_error));
    }
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU
    const esp_err_t imu_error = LUNASHELL_hardware_set_imu_motion_wakeup(false);
    if (imu_error != ESP_OK) {
        ESP_LOGE(kTag, "Deep-sleep IMU recovery failed (%s)", esp_err_to_name(imu_error));
    }
    s_recovery_complete = release_error == ESP_OK && wake_error == ESP_OK &&
        input_error == ESP_OK && imu_error == ESP_OK;
#else
    s_recovery_complete = release_error == ESP_OK && wake_error == ESP_OK && input_error == ESP_OK;
#endif
    return result;
}

// No parameters: never permit an active rollback while physical sleep recovery failed.
bool recovery_complete()
{
    return s_recovery_complete;
}

}  // namespace LUNASHELL::sleep
