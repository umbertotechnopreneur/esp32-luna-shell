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

#include "CODY_status_overlay.hpp"
#include "CODY_theme_generated.hpp"
#include "CODY_pixel_glyphs.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "LUNASHELL_clock.hpp"
#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_status";
constexpr uint16_t kPanelSize = 466;
using pixel_glyphs::find_glyph;
using pixel_glyphs::kGlyphColumns;
using pixel_glyphs::kGlyphRows;
using pixel_glyphs::MonoGlyph;
constexpr uint16_t kCharacterAdvance = 6;
// The fixed positions follow the upper circular edge while preserving room for
// the centered connection caption and the maximum-width status text.
constexpr uint16_t kDateLeft = 188;
constexpr uint16_t kDateTop = 20;
constexpr uint16_t kTimeLeft = 204;
constexpr uint16_t kTimeTop = 34;
constexpr uint16_t kBatteryLeft = 335;
constexpr uint16_t kBatteryTop = 57;
constexpr uint16_t kBatteryTextLeft = 364;
constexpr uint16_t kBatteryTextTop = 64;
constexpr uint16_t kWifiLeft = 100;
constexpr uint16_t kWifiTop = 57;
constexpr uint16_t kBluetoothLeft = 155;
constexpr uint16_t kBluetoothTop = 30;
constexpr uint16_t kDateLight = Theme::chrome_date_on_dark;
constexpr uint16_t kDateDark = Theme::chrome_date_on_light;
constexpr uint16_t kTimeLight = Theme::chrome_time_on_dark;
constexpr uint16_t kTimeDark = Theme::chrome_time_on_light;
constexpr uint16_t kReady = Theme::status_ready;
constexpr uint16_t kWarning = Theme::status_warning;
constexpr uint16_t kMissing = Theme::status_missing;
constexpr uint16_t kFault = Theme::status_error;
constexpr uint16_t kMuted = Theme::status_unavailable;
constexpr uint16_t kActive = Theme::status_active;
constexpr uint16_t kChargeMark = Theme::text_bright;


// Compact icon rows are compile-time constants, so ESP-IDF keeps the glyphs in flash rodata.
struct IconGlyph {
    uint8_t width;
    uint8_t height;
    uint32_t rows[21];
};

constexpr IconGlyph kBatteryGlyph = {
    26, 16,
    {
        0x001FFFFC, 0x003FFFFE, 0x00600003, 0x00600003,
        0x00600003, 0x03E00003, 0x03E00003, 0x03E00003,
        0x03E00003, 0x03E00003, 0x03E00003, 0x00600003,
        0x00600003, 0x00600003, 0x003FFFFE, 0x001FFFFC,
    },
};

constexpr IconGlyph kChargeGlyph = {
    9, 12,
    {0x060, 0x070, 0x038, 0x03C, 0x01E, 0x1FF,
     0x0FF, 0x078, 0x03C, 0x01C, 0x00C, 0x004},
};

// Four fixed glyphs share one footprint: unavailable dot, then one/two/three arcs.
constexpr IconGlyph kWifiGlyphs[] = {
    {21, 17, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xE00, 0x1F00, 0x1F00, 0xE00}},
    {21, 17, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1F00, 0x71C0, 0xC060, 0,
              0xE00, 0x1F00, 0x1F00, 0xE00}},
    {21, 17, {0, 0, 0, 0, 0, 0x1FFF0, 0x38038, 0x6000C, 0, 0x1F00, 0x71C0, 0xC060, 0,
              0xE00, 0x1F00, 0x1F00, 0xE00}},
    {21, 17, {0x1FFF0, 0x7FFFC, 0xE000E, 0x1C0007, 0, 0x1FFF0, 0x38038, 0x6000C, 0,
              0x1F00, 0x71C0, 0xC060, 0, 0xE00, 0x1F00, 0x1F00, 0xE00}},
};

constexpr IconGlyph kBluetoothGlyph = {
    13, 21,
    {
        0x060, 0x0E0, 0x1E0, 0x360, 0x660, 0xC66, 0x66C,
        0x378, 0x1F0, 0x0E0, 0x060, 0x0E0, 0x1F0, 0x378,
        0x66C, 0xC66, 0x660, 0x360, 0x1E0, 0x0E0, 0x060,
    },
};

// x: Farthest horizontal distance from the panel center.
// y: Farthest vertical distance from the panel center.
constexpr bool inside_panel(int x, int y)
{
    return x * x + y * y < 233 * 233;
}

static_assert(inside_panel(133, 176) && inside_panel(78, 203) &&
    inside_panel(45, 213) && inside_panel(128, 176) && inside_panel(159, 169),
    "Status text and icons must fit every occupied circular display row");
static_assert(kStatusOverlayBounds.left == kWifiLeft && kStatusOverlayBounds.top == kDateTop &&
    kBatteryTextLeft + 5 * kCharacterAdvance <= kStatusOverlayBounds.right &&
    kStatusOverlayBounds.bottom == kWifiTop + kWifiGlyphs[2].height,
    "Shared status exclusion must match the actual authored chrome footprint");
static_assert(kTimeLeft >= kStatusOverlayBounds.left && kTimeTop >= kStatusOverlayBounds.top &&
    kTimeTop + kGlyphRows * 2 <= kStatusOverlayBounds.bottom &&
    kBatteryLeft + kBatteryGlyph.width <= kStatusOverlayBounds.right &&
    kBatteryTop + kBatteryGlyph.height <= kStatusOverlayBounds.bottom &&
    kWifiLeft + kWifiGlyphs[2].width <= kStatusOverlayBounds.right &&
    kWifiTop + kWifiGlyphs[2].height <= kStatusOverlayBounds.bottom &&
    kBluetoothLeft + kBluetoothGlyph.width <= kStatusOverlayBounds.right &&
    kBluetoothTop + kBluetoothGlyph.height <= kStatusOverlayBounds.bottom,
    "Time and status icons must remain within the shared reserved footprint");


constexpr std::array<const char *, 7> kWeekdays = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
};
constexpr std::array<const char *, 12> kMonths = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
};

// component: Canonical health fields whose semantic color is requested.
StatusTone component_tone(const LUNASHELL::ComponentSnapshot &component)
{
    if (component.data_stale) {
        return StatusTone::Warning;
    }
    switch (component.health) {
    case LUNASHELL::DeviceHealth::Ready:
        if (component.last_error != ESP_OK) {
            return StatusTone::Fault;
        }
        return component.data_valid ? StatusTone::Ready : StatusTone::Warning;
    case LUNASHELL::DeviceHealth::Degraded:
        return StatusTone::Warning;
    case LUNASHELL::DeviceHealth::Missing:
        return StatusTone::Missing;
    case LUNASHELL::DeviceHealth::Unknown:
        if (component.last_attempt_at_us == 0 && component.last_success_at_us == 0 &&
            component.last_error == ESP_ERR_INVALID_STATE && !component.data_valid) {
            return StatusTone::Muted;
        }
        return component.last_error == ESP_OK ? StatusTone::Warning : StatusTone::Fault;
    }
    return StatusTone::Fault;
}

// tone: Typed semantic role shared with the Luna palette.
uint16_t tone_color(StatusTone tone)
{
    switch (tone) {
    case StatusTone::Ready:
        return kReady;
    case StatusTone::Warning:
        return kWarning;
    case StatusTone::Missing:
        return kMissing;
    case StatusTone::Fault:
        return kFault;
    case StatusTone::Muted:
        return kMuted;
    case StatusTone::Active:
        return kActive;
    }
    return kFault;
}


// strip: Validated compact RGB565 target.
// first_row: Logical y origin of the compact target.
// first_column: Logical x origin of the compact target.
// canvas_x: Logical display x coordinate to clip.
// canvas_y: Logical display y coordinate to clip.
// stored_color: Color already encoded for the target byte order.
void put_pixel(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column,
               int canvas_x, int canvas_y, uint16_t stored_color)
{
    if (canvas_x < first_column || canvas_x >= first_column + strip.width ||
        canvas_y < first_row || canvas_y >= first_row + strip.height) {
        return;
    }
    strip.pixels[(canvas_y - first_row) * strip.stride + canvas_x - first_column] = stored_color;
}

// glyph: Immutable row-mask icon stored in flash rodata.
// left: Logical x coordinate of the icon's left edge.
// top: Logical y coordinate of the icon's top edge.
// strip: Validated compact RGB565 target.
// first_row: Logical y origin of the compact target.
// first_column: Logical x origin of the compact target.
// stored_color: Color already encoded for the target byte order.
void draw_icon_glyph(const IconGlyph &glyph, int left, int top,
                     CODY2D::Surface strip, uint16_t first_row,
                     uint16_t first_column, uint16_t stored_color)
{
    for (uint8_t row = 0; row < glyph.height; ++row) {
        if (top + row < first_row || top + row >= first_row + strip.height) continue;
        const uint32_t row_mask = glyph.rows[row];
        for (uint8_t column = 0; column < glyph.width; ++column) {
            if ((row_mask & (1UL << column)) != 0) {
                put_pixel(strip, first_row, first_column,
                          left + column, top + row, stored_color);
            }
        }
    }
}

// text: Bounded null-terminated status text.
// left: Logical x coordinate of the first glyph.
// top: Logical y coordinate of the first glyph row.
// scale: One for metadata or two for the current HH:MM clock.
// strip: Validated compact RGB565 target.
// first_row: Logical y origin of the compact target.
// first_column: Logical x origin of the compact target.
// stored_color: Color already encoded for the target byte order.
// Exception: Returns an unsupported-glyph or invalid-size error.
esp_err_t draw_text(const char *text, int left, int top, uint8_t scale, CODY2D::Surface strip,
                    uint16_t first_row, uint16_t first_column, uint16_t stored_color)
{
    const size_t length = std::strlen(text);
    if (length == 0 || length > 31 || scale < 1 || scale > 2) {
        ESP_LOGE(kTag, "Status text length rejected: %u (%s)",
                 static_cast<unsigned>(length), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const int right = left + static_cast<int>((length * kCharacterAdvance - 1) * scale) - 1;
    const int bottom = top + kGlyphRows * scale - 1;
    const int farthest_x = std::max(std::abs(left - 233), std::abs(right - 233));
    const int farthest_y = std::max(std::abs(top - 233), std::abs(bottom - 233));
    if (farthest_x * farthest_x + farthest_y * farthest_y >= 233 * 233) {
        ESP_LOGE(kTag, "Status text exceeds circular layout: x=%d y=%d scale=%u (%s)",
                 left, top, static_cast<unsigned>(scale), esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t index = 0; index < length; ++index) {
        const MonoGlyph *glyph = find_glyph(text[index]);
        if (glyph == nullptr) {
            ESP_LOGE(kTag, "Status glyph rejected: ASCII=%u (%s)",
                     static_cast<unsigned>(static_cast<uint8_t>(text[index])),
                     esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
            return ESP_ERR_NOT_SUPPORTED;
        }
        for (uint16_t column = 0; column < kGlyphColumns; ++column) {
            for (uint16_t row = 0; row < kGlyphRows; ++row) {
                const int glyph_top = top + row * scale;
                if (glyph_top + scale <= first_row || glyph_top >= first_row + strip.height) continue;
                if ((glyph->columns[column] & (1U << row)) != 0) {
                    for (uint8_t dy = 0; dy < scale; ++dy) {
                        for (uint8_t dx = 0; dx < scale; ++dx) {
                            put_pixel(strip, first_row, first_column,
                                left + static_cast<int>((index * kCharacterAdvance + column) * scale) + dx,
                                top + row * scale + dy, stored_color);
                        }
                    }
                }
            }
        }
    }
    return ESP_OK;
}

// overlay: Prepared battery level, validity and charging state.
// strip: Validated compact RGB565 target.
// first_row: Logical y origin of the compact target.
// first_column: Logical x origin of the compact target.
// stored_color: Semantic battery color encoded for the target byte order.
// charge_color: Charging bolt color encoded for the target byte order.
void draw_battery(const StatusOverlay &overlay, CODY2D::Surface strip,
                  uint16_t first_row, uint16_t first_column,
                  uint16_t stored_color, uint16_t charge_color)
{
    draw_icon_glyph(
        kBatteryGlyph, kBatteryLeft, kBatteryTop,
        strip, first_row, first_column, stored_color
    );
    if (overlay.battery_present && overlay.battery_percent_valid) {
        constexpr int interior_width = 15;
        const int fill_width =
            (overlay.battery_percent * interior_width + 99) / 100;
        for (int y = 4; y <= 11; ++y) {
            for (int x = 0; x < fill_width; ++x) {
                put_pixel(strip, first_row, first_column, kBatteryLeft + 4 + x,
                          kBatteryTop + y, stored_color);
            }
        }
    }
    if (overlay.battery_charging) {
        draw_icon_glyph(
            kChargeGlyph, kBatteryLeft + 7, kBatteryTop + 2,
            strip, first_row, first_column, charge_color
        );
    }
}

// bars: Zero selects unavailable/offline artwork; one through three are measured levels.
// disconnected: Confirmed lack of network connection uses a crossed red full glyph.
// strip: Validated compact RGB565 target.
// first_row: Logical y origin of the compact target.
// first_column: Logical x origin of the compact target.
// stored_color: Semantic Wi-Fi color encoded for the target byte order.
void draw_wifi(uint8_t bars, bool disconnected, CODY2D::Surface strip, uint16_t first_row,
               uint16_t first_column, uint16_t stored_color)
{
    const uint8_t visible_bars = disconnected ? 3 : std::min<uint8_t>(bars, 3);
    draw_icon_glyph(
        kWifiGlyphs[visible_bars], kWifiLeft, kWifiTop,
        strip, first_row, first_column, stored_color
    );
    if (disconnected) {
        for (int y = 0; y < 17; ++y) {
            const int x = 18 - y;
            put_pixel(strip, first_row, first_column, kWifiLeft + x, kWifiTop + y, stored_color);
            put_pixel(strip, first_row, first_column, kWifiLeft + x - 1, kWifiTop + y, stored_color);
        }
    }
}

// strip: Validated compact RGB565 target.
// first_row: Logical y origin of the compact target.
// first_column: Logical x origin of the compact target.
// stored_color: Semantic Bluetooth color encoded for the target byte order.
void draw_bluetooth(CODY2D::Surface strip, uint16_t first_row,
                    uint16_t first_column, uint16_t stored_color)
{
    draw_icon_glyph(
        kBluetoothGlyph, kBluetoothLeft, kBluetoothTop,
        strip, first_row, first_column, stored_color
    );
}

// destination: Bounded output string.
// capacity: Writable bytes including the terminator.
// format: Fixed compile-time format for numeric status values.
// value: Validated whole-number value.
// Exception: Returns an invalid-size error when bounded formatting cannot complete.
esp_err_t format_number(char *destination, size_t capacity, const char *format, int value)
{
    const int written = std::snprintf(destination, capacity, format, value);
    if (written <= 0 || static_cast<size_t>(written) >= capacity) {
        ESP_LOGE(kTag, "Status number formatting failed: value=%d (%s)",
                 value, esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

}  // namespace

esp_err_t prepare_status_caption(const char *text, uint16_t color, StatusCaption &caption)
{
    if (text == nullptr) return ESP_ERR_INVALID_ARG;
    StatusCaption next{};
    size_t length = 0;
    while (length < next.text.size() && text[length] != '\0') {
        if (find_glyph(text[length]) == nullptr) return ESP_ERR_NOT_SUPPORTED;
        ++length;
    }
    if (length == 0 || length == next.text.size()) return ESP_ERR_INVALID_SIZE;
    std::memcpy(next.text.data(), text, length + 1);
    next.color = color;
    next.valid = true;
    caption = next;
    return ESP_OK;
}

esp_err_t draw_status_caption_rows(const StatusCaption &caption, CODY2D::Surface strip,
    uint16_t first_row, uint16_t first_column)
{
    if (!caption.valid) return ESP_ERR_INVALID_STATE;
    const esp_err_t surface_error = CODY2D::validate_surface(strip);
    if (surface_error != ESP_OK) return surface_error;
    if (first_column + strip.width > kPanelSize || first_row + strip.height > kPanelSize) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t length = 0;
    while (length < caption.text.size() && caption.text[length] != '\0') ++length;
    if (length == 0 || length == caption.text.size()) return ESP_ERR_INVALID_SIZE;
    const int width = static_cast<int>(length * kCharacterAdvance - 1);
    return draw_text(caption.text.data(), (kPanelSize - width) / 2, kStatusCaptionBounds.top, 1,
        strip, first_row, first_column, CODY2D::rgb565_native_to_stored(caption.color, strip.byte_order));
}

// No parameters: return the complete half-open footprint shared by damage planning.
DamageRect status_overlay_damage_bounds()
{
    return kStatusOverlayBounds;
}

// observation: Coherent canonical battery, local calendar, Wi-Fi and BLE state.
// overlay: Receives a complete truthful presentation only after bounded formatting.
// show_battery_percentage: Whether the valid numeric percentage accompanies the icon.
// Exception: Returns a formatting or snapshot-contract error without plausible fallback values.
esp_err_t prepare_status_overlay(const LUNASHELL::HardwareStatus &observation,
                                 StatusOverlay &overlay, bool show_battery_percentage)
{
    StatusOverlay next;
    const auto &battery = observation.battery;
    next.battery_tone = component_tone(battery);
    if (battery.data_stale) {
        std::snprintf(next.battery, sizeof(next.battery), "STALE");
        next.battery_tone = StatusTone::Warning;
    } else if (battery.health == LUNASHELL::DeviceHealth::Ready &&
               battery.last_error == ESP_OK && battery.data_valid) {
        next.battery_present = battery.battery_present;
        if (!battery.battery_present) {
            next.battery[0] = '\0';
            next.battery_tone = StatusTone::Muted;
        } else if (!battery.level_percent_valid || battery.level_percent < 0 ||
                   battery.level_percent > 100) {
            std::snprintf(next.battery, sizeof(next.battery), "ERR");
            next.battery_tone = StatusTone::Fault;
        } else {
            const esp_err_t format_error = format_number(
                next.battery, sizeof(next.battery), "%d%%", battery.level_percent
            );
            if (format_error != ESP_OK) {
                return format_error;
            }
            next.battery_percent_valid = true;
            next.battery_percent = static_cast<uint8_t>(battery.level_percent);
            next.battery_tone = battery.level_percent <= 15 ? StatusTone::Fault :
                battery.level_percent <= 50 ? StatusTone::Warning : StatusTone::Ready;
        }
        next.battery_charging = battery.battery_present && battery.usb_present &&
            std::strcmp(battery.power_state, "charging") == 0;
        if (next.battery_charging) {
            next.battery_tone = StatusTone::Active;
        }
    } else if (battery.health == LUNASHELL::DeviceHealth::Missing ||
               next.battery_tone == StatusTone::Muted) {
        next.battery[0] = '\0';
        next.battery_tone = StatusTone::Muted;
    } else {
        std::snprintf(next.battery, sizeof(next.battery), "ERR");
        next.battery_tone = StatusTone::Fault;
    }

    const auto &clock = observation.clock;
    next.clock_tone = component_tone(clock);
    const bool calendar_valid = clock.local_year >= 2020 && clock.local_year <= 9999 &&
        clock.local_month >= 1 && clock.local_month <= 12 &&
        clock.local_day >= 1 && clock.local_day <= 31 &&
        clock.local_weekday < kWeekdays.size() && clock.local_minute < 24 * 60 &&
        clock.local_second < 60;
    if (LUNASHELL::clock_observation_current(clock) && calendar_valid) {
        const int date_written = std::snprintf(
            next.date, sizeof(next.date), "%s %02u %s %04u",
            kWeekdays[clock.local_weekday], static_cast<unsigned>(clock.local_day),
            kMonths[clock.local_month - 1], static_cast<unsigned>(clock.local_year)
        );
        const int time_written = std::snprintf(
            next.time, sizeof(next.time), "%02u:%02u",
            static_cast<unsigned>(clock.local_minute / 60),
            static_cast<unsigned>(clock.local_minute % 60)
        );
        if (date_written <= 0 || static_cast<size_t>(date_written) >= sizeof(next.date) ||
            time_written <= 0 || static_cast<size_t>(time_written) >= sizeof(next.time)) {
            ESP_LOGE(kTag, "Clock status formatting failed (%s)",
                     esp_err_to_name(ESP_ERR_INVALID_SIZE));
            return ESP_ERR_INVALID_SIZE;
        }
        next.clock_tone = StatusTone::Ready;
    } else {
        std::snprintf(next.date, sizeof(next.date), "TIME");
        std::snprintf(next.time, sizeof(next.time),
                      clock.data_stale ? "STALE" : "UNAVAILABLE");
        if (LUNASHELL::clock_observation_current(clock) && !calendar_valid) {
            next.clock_tone = StatusTone::Fault;
            ESP_LOGE(kTag, "Current clock calendar rejected: %u-%u-%u weekday=%u minute=%u second=%u",
                     static_cast<unsigned>(clock.local_year),
                     static_cast<unsigned>(clock.local_month),
                     static_cast<unsigned>(clock.local_day),
                     static_cast<unsigned>(clock.local_weekday),
                     static_cast<unsigned>(clock.local_minute),
                     static_cast<unsigned>(clock.local_second));
        }
    }

    const auto &wifi = observation.wifi;
    next.wifi_tone = component_tone(wifi);
    if (wifi.connection_state_valid && !wifi.data_stale && wifi.last_attempt_at_us > 0 &&
        (!wifi.connected || !wifi.ip_acquired)) {
        next.wifi_tone = StatusTone::Missing;
    } else if (wifi.health == LUNASHELL::DeviceHealth::Ready && wifi.last_error == ESP_OK &&
        wifi.data_valid && !wifi.data_stale) {
        if (!wifi.connected || !wifi.ip_acquired) {
            next.wifi_tone = StatusTone::Missing;
        } else if (wifi.rssi_valid && wifi.rssi_error == ESP_OK && wifi.rssi < 0) {
            next.wifi_tone = wifi.rssi <= -80 ? StatusTone::Warning : StatusTone::Ready;
            next.wifi_bars = wifi.rssi >= -60 ? 3 : wifi.rssi >= -72 ? 2 : 1;
        } else {
            // An associated link without a fresh measurement has unknown strength.
            next.wifi_tone = wifi.rssi_error == ESP_OK || wifi.rssi_error == ESP_ERR_TIMEOUT
                ? StatusTone::Warning : StatusTone::Fault;
        }
    } else if (wifi.health == LUNASHELL::DeviceHealth::Missing) {
        next.wifi_tone = StatusTone::Missing;
    }

    const auto &bluetooth = observation.ble;
    next.bluetooth_tone = component_tone(bluetooth);
    if (bluetooth.health == LUNASHELL::DeviceHealth::Ready &&
        bluetooth.last_error == ESP_OK && bluetooth.data_valid && !bluetooth.data_stale) {
        next.bluetooth_active = true;
        next.bluetooth_tone = bluetooth.connected
            ? StatusTone::Ready : StatusTone::Active;
    } else if (bluetooth.health == LUNASHELL::DeviceHealth::Missing ||
               next.bluetooth_tone == StatusTone::Muted) {
        next.bluetooth_tone = StatusTone::Muted;
    }

    if (!show_battery_percentage && next.battery_percent_valid) next.battery[0] = '\0';
    overlay = next;
    return ESP_OK;
}

// left: Previously completed presentation.
// right: Current prepared presentation.
bool status_overlay_equal(const StatusOverlay &left, const StatusOverlay &right)
{
    return std::strcmp(left.date, right.date) == 0 &&
        std::strcmp(left.time, right.time) == 0 &&
        std::strcmp(left.battery, right.battery) == 0 &&
        left.clock_tone == right.clock_tone &&
        left.battery_tone == right.battery_tone &&
        left.wifi_tone == right.wifi_tone &&
        left.bluetooth_tone == right.bluetooth_tone &&
        left.battery_percent == right.battery_percent &&
        left.wifi_bars == right.wifi_bars &&
        left.battery_percent_valid == right.battery_percent_valid &&
        left.battery_present == right.battery_present &&
        left.battery_charging == right.battery_charging &&
        left.bluetooth_active == right.bluetooth_active;
}

namespace {

// Draw all status artwork at full opacity, including overlapping battery artwork.
esp_err_t draw_opaque_status_rows(const StatusOverlay &overlay, uint16_t background_rgb565,
    CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const bool white_background = background_rgb565 == Theme::surface_light;
    const uint16_t date_color = CODY2D::rgb565_native_to_stored(
        white_background ? kDateDark : kDateLight, strip.byte_order);
    // Neutral pearl gives a valid clock hierarchy; errors retain their semantic color.
    const uint16_t clock_ink = overlay.clock_tone == StatusTone::Ready
        ? (white_background ? kTimeDark : kTimeLight) : tone_color(overlay.clock_tone);
    const uint16_t clock_color = CODY2D::rgb565_native_to_stored(
        clock_ink, strip.byte_order);
    const uint16_t battery_color = CODY2D::rgb565_native_to_stored(
        tone_color(overlay.battery_tone), strip.byte_order);
    const uint16_t wifi_color = CODY2D::rgb565_native_to_stored(
        tone_color(overlay.wifi_tone), strip.byte_order);
    const uint16_t bluetooth_color = CODY2D::rgb565_native_to_stored(
        tone_color(overlay.bluetooth_tone), strip.byte_order);
    const uint16_t charge_color = CODY2D::rgb565_native_to_stored(
        kChargeMark, strip.byte_order);

    esp_err_t result = draw_text(
        overlay.date, kDateLeft, kDateTop, 1,
        strip, first_row, first_column, date_color
    );
    if (result == ESP_OK) {
        result = draw_text(
            overlay.time, kTimeLeft, kTimeTop, overlay.clock_tone == StatusTone::Ready ? 2 : 1,
            strip, first_row, first_column, clock_color
        );
    }
    if (result != ESP_OK) {
        return result;
    }
    draw_battery(overlay, strip, first_row, first_column, battery_color, charge_color);
    if (overlay.battery[0] != '\0') {
        result = draw_text(overlay.battery, kBatteryTextLeft, kBatteryTextTop, 1,
                           strip, first_row, first_column, battery_color);
        if (result != ESP_OK) {
            return result;
        }
    }
    draw_wifi(overlay.wifi_bars, overlay.wifi_tone == StatusTone::Missing, strip, first_row, first_column, wifi_color);
    draw_bluetooth(strip, first_row, first_column, bluetooth_color);
    return ESP_OK;
}

} // namespace

// overlay: Prepared immutable text and semantic icon state.
// opacity: Shared touch-chrome fade from transparent zero to opaque 255.
// background_rgb565: Authored scene color, used to choose light or dark text.
// strip: Compact RGB565 destination containing the actual underlying scene.
// first_row: Logical row represented by the strip's first pixel row.
// first_column: Logical column represented by the strip's first pixel.
// Exception: Returns the real surface, geometry or glyph error.
esp_err_t draw_status_overlay_rows(const StatusOverlay &overlay, uint8_t opacity,
                                   uint16_t background_rgb565, CODY2D::Surface strip,
                                   uint16_t first_row, uint16_t first_column)
{
    const esp_err_t surface_error = CODY2D::validate_surface(strip);
    if (surface_error != ESP_OK) return surface_error;
    if (first_column + strip.width > kPanelSize || first_row + strip.height > kPanelSize) {
        ESP_LOGE(kTag, "Status strip rejected: column=%u width=%u row=%u height=%u (%s)",
                 first_column, strip.width, first_row, strip.height,
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    if (opacity == 0) return ESP_OK;
    if (opacity == 255) {
        return draw_opaque_status_rows(overlay, background_rgb565, strip, first_row, first_column);
    }

    // Compose the complete opaque row once over the saved scene. This avoids
    // blending overlapping strokes twice (for example the charging bolt).
    std::array<uint16_t, kPanelSize> scene_row{};
    const uint16_t top = std::max<uint16_t>(first_row, kStatusOverlayBounds.top);
    const uint16_t bottom = std::min<uint16_t>(first_row + strip.height, kStatusOverlayBounds.bottom);
    for (uint16_t y = top; y < bottom; ++y) {
        uint16_t *const pixels = strip.pixels + (y - first_row) * strip.stride;
        std::copy_n(pixels, strip.width, scene_row.begin());
        CODY2D::Surface row{pixels, strip.width, 1, strip.width, strip.width, strip.byte_order};
        const esp_err_t result = draw_opaque_status_rows(overlay, background_rgb565, row, y, first_column);
        for (uint16_t x = 0; x < strip.width; ++x) {
            if (pixels[x] != scene_row[x]) {
                const uint16_t foreground = CODY2D::rgb565_stored_to_native(pixels[x], strip.byte_order);
                const uint16_t background = CODY2D::rgb565_stored_to_native(scene_row[x], strip.byte_order);
                pixels[x] = CODY2D::rgb565_native_to_stored(
                    CODY2D::blend_rgb565(foreground, background, opacity), strip.byte_order);
            }
        }
        if (result != ESP_OK) return result;
    }
    return ESP_OK;
}

}  // namespace CODY
