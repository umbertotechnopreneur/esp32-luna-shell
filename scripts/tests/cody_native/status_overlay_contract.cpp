#include "CODY_status_overlay.hpp"
#include "LUNASHELL_clock.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace LUNASHELL {

// clock: Explicit host fixture; mirrors the time owner's current-observation gate.
// This test links the real formatter/raster, not the hardware time service.
bool clock_observation_current(const ClockSnapshot &clock)
{
    return clock.preferences_valid && clock.health == DeviceHealth::Ready &&
        clock.last_error == ESP_OK && clock.data_valid && !clock.data_stale &&
        clock.last_success_at_us > 0;
}

} // namespace LUNASHELL

namespace {

int failures = 0;
constexpr uint16_t kCanvas = 466;

// condition: Observable contract of the real status formatter or raster.
// message: Specific failed behavior retained by the native runner.
void require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "Status overlay failure: %s\n", message);
        ++failures;
    }
}

// component: Host fixture explicitly marked as a current measured observation.
void current(LUNASHELL::ComponentSnapshot &component)
{
    component.health = LUNASHELL::DeviceHealth::Ready;
    component.last_error = ESP_OK;
    component.data_valid = true;
    component.data_stale = false;
    component.last_success_at_us = 1000000;
}

// No parameters: return synthetic input confined to this hardware-free executable.
LUNASHELL::DeviceStatusObservation measured_observation()
{
    LUNASHELL::DeviceStatusObservation observation;
    current(observation.clock);
    observation.clock.preferences_valid = true;
    observation.clock.local_year = 2026;
    observation.clock.local_month = 10;
    observation.clock.local_day = 5;
    observation.clock.local_weekday = 1;
    observation.clock.local_minute = 9 * 60 + 7;
    observation.clock.local_second = 3;
    current(observation.battery);
    observation.battery.battery_present = true;
    observation.battery.level_percent_valid = true;
    observation.battery.level_percent = 100;
    current(observation.wifi);
    observation.wifi.started = true;
    observation.wifi.credentials_configured = true;
    observation.wifi.connected = true;
    observation.wifi.ip_acquired = true;
    observation.wifi.rssi = -51;
    current(observation.ble);
    observation.ble.connected = true;
    return observation;
}

// overlay: Real formatted text and status masks whose complete footprint is checked.
// background: Authored black or white scene background.
// opacity: Shared chrome opacity to exercise without changing geometry.
// Exception: Host vector allocation may throw std::bad_alloc.
std::vector<uint16_t> render(const CODY::StatusOverlay &overlay, uint16_t background, uint8_t opacity)
{
    std::vector<uint16_t> pixels(kCanvas * kCanvas, background);
    CODY2D::Surface surface{pixels.data(), kCanvas, kCanvas, kCanvas,
        static_cast<uint32_t>(pixels.size())};
    require(CODY::draw_status_overlay_rows(overlay, opacity, background, surface, 0, 0) == ESP_OK,
        "prepared overlay renders without an error");
    const auto damage = CODY::status_overlay_damage_bounds();
    bool painted = false;
    for (int y = 0; y < kCanvas; ++y) {
        for (int x = 0; x < kCanvas; ++x) {
            if (pixels[y * kCanvas + x] == background) {
                continue;
            }
            painted = true;
            require(x >= damage.left && x < damage.right && y >= damage.top && y < damage.bottom,
                "every painted status pixel belongs to its shared damage footprint");
            const int dx = x - 233;
            const int dy = y - 233;
            require(dx * dx + dy * dy < 233 * 233,
                "every painted status pixel stays inside the physical circle");
        }
    }
    require(painted == (opacity != 0), "chrome opacity zero suppresses all status pixels");
    return pixels;
}

// overlay: Valid presentation drawn across narrow compact tiles.
// reference: Complete-frame raster from the same opacity and background.
// Exception: Host vector allocation may throw std::bad_alloc.
void compact_tile_parity(const CODY::StatusOverlay &overlay, const std::vector<uint16_t> &reference)
{
    constexpr uint16_t guard = 0x1357;
    for (uint16_t top = 0; top < 72; top += 9) {
        for (uint16_t left = 0; left < kCanvas; left += 43) {
            const uint16_t width = std::min<uint16_t>(43, kCanvas - left);
            const uint16_t stride = width + 2;
            constexpr uint16_t height = 9;
            std::vector<uint16_t> pixels(stride * (height + 2), guard);
            for (uint16_t y = 1; y <= height; ++y) {
                std::fill_n(pixels.data() + y * stride + 1, width, 0);
            }
            CODY2D::Surface tile{pixels.data() + stride + 1, width, height, stride,
                static_cast<uint32_t>(pixels.size() - stride - 1)};
            require(CODY::draw_status_overlay_rows(overlay, 128, 0, tile, top, left) == ESP_OK,
                "status compact tile renders successfully");
            for (uint16_t y = 0; y < height + 2; ++y) {
                for (uint16_t x = 0; x < stride; ++x) {
                    const bool interior = y > 0 && y <= height && x > 0 && x <= width;
                    const uint16_t expected = interior
                        ? reference[(top + y - 1) * kCanvas + left + x - 1] : guard;
                    require(pixels[y * stride + x] == expected,
                        "compact status tiles match full raster and preserve all sentinel padding");
                }
            }
        }
    }
}

} // namespace

// No parameters: exercise only the portable native status formatter and raster.
// Exception: Host raster allocations may throw std::bad_alloc.
int check_status_overlay_contract()
{
    failures = 0;
    auto observation = measured_observation();
    CODY::StatusOverlay overlay;
    require(CODY::prepare_status_overlay(observation, overlay) == ESP_OK,
        "current status formatting succeeds");
    require(std::strcmp(overlay.time, "09:07") == 0 &&
        std::strcmp(overlay.date, "MON 05 OCT 2026") == 0,
        "local clock uses HH:MM while retaining the complete English date");
    observation.clock.local_second = 59;
    CODY::StatusOverlay second_changed;
    require(CODY::prepare_status_overlay(observation, second_changed) == ESP_OK &&
        CODY::status_overlay_equal(overlay, second_changed),
        "seconds alone do not redraw the visible clock");
    observation.clock.local_minute += 1;
    CODY::StatusOverlay minute_changed;
    require(CODY::prepare_status_overlay(observation, minute_changed) == ESP_OK &&
        !CODY::status_overlay_equal(overlay, minute_changed),
        "a changed local minute invalidates the clock");

    render(overlay, 0, 255);
    render(overlay, 0xFFFF, 255);
    render(overlay, 0, 0);
    compact_tile_parity(overlay, render(overlay, 0, 128));

    observation.battery.battery_present = false;
    CODY::StatusOverlay absent;
    require(CODY::prepare_status_overlay(observation, absent) == ESP_OK &&
        !absent.battery_percent_valid && absent.battery[0] == '\0' &&
        absent.battery_tone == CODY::StatusTone::Muted,
        "absent battery stays gray and never presents a numeric percentage");
    render(absent, 0, 255);

    observation = measured_observation();
    observation.battery.usb_present = true;
    std::strcpy(observation.battery.power_state, "charging");
    CODY::StatusOverlay charging;
    require(CODY::prepare_status_overlay(observation, charging) == ESP_OK &&
        charging.battery_charging && charging.battery_tone == CODY::StatusTone::Active,
        "charging retains the cyan semantic battery and independent white bolt");
    render(charging, 0, 255);

    observation.clock.data_stale = true;
    observation.battery.data_stale = true;
    observation.wifi.data_stale = true;
    observation.ble.data_stale = true;
    CODY::StatusOverlay stale;
    require(CODY::prepare_status_overlay(observation, stale) == ESP_OK &&
        std::strcmp(stale.time, "STALE") == 0 && std::strcmp(stale.battery, "STALE") == 0 &&
        !stale.battery_percent_valid && stale.wifi_tone == CODY::StatusTone::Warning &&
        stale.wifi_bars == 0 && stale.bluetooth_tone == CODY::StatusTone::Warning &&
        !stale.bluetooth_active,
        "stale samples do not masquerade as current clock, charge, signal or Bluetooth activity");
    render(stale, 0, 255);

    observation = {};
    CODY::StatusOverlay unavailable;
    require(CODY::prepare_status_overlay(observation, unavailable) == ESP_OK &&
        std::strcmp(unavailable.time, "UNAVAILABLE") == 0,
        "missing clock keeps an explicit unavailable label");
    render(unavailable, 0, 255);
    return failures;
}
