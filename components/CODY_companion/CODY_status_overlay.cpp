#include "CODY_status_overlay.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "LUNASHELL_clock.hpp"
#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_status";
constexpr uint16_t kPanelSize = 466;
constexpr uint16_t kPanelCenter = 233;
constexpr uint16_t kGlyphColumns = 5;
constexpr uint16_t kGlyphRows = 7;
constexpr uint16_t kCharacterAdvance = 6;
constexpr uint16_t kDateTop = 5;
constexpr uint16_t kTimeTop = 15;
constexpr uint16_t kBatteryLeft = 96;
constexpr uint16_t kBatteryTop = 43;
constexpr uint16_t kBatteryTextLeft = 123;
constexpr uint16_t kBatteryTextTop = 45;
constexpr uint16_t kWifiCenterX = 351;
constexpr uint16_t kWifiCenterY = 56;
constexpr uint16_t kReady = 0x26F8;   // #20dfc1 in native RGB565.
constexpr uint16_t kWarning = 0xF5A9; // #f0b44d in native RGB565.
constexpr uint16_t kMissing = 0xEA6B; // #eb4d5c in native RGB565.
constexpr uint16_t kFault = 0xF9CB;   // #ff3859 in native RGB565.
constexpr uint16_t kMuted = 0x63F2;   // #617f91 in native RGB565.
constexpr uint16_t kCharge = 0xFFFF;

struct MonoGlyph {
    char character;
    uint8_t columns[kGlyphColumns];
};

// Full uppercase ASCII needed by dates and explicit unavailable states.
constexpr MonoGlyph kGlyphs[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'%', {0x63, 0x13, 0x08, 0x64, 0x63}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x7F, 0x20, 0x18, 0x20, 0x7F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
};

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
    }
    return kFault;
}

// character: Printable uppercase ASCII character from the bounded status alphabet.
const MonoGlyph *find_glyph(char character)
{
    for (const MonoGlyph &glyph : kGlyphs) {
        if (glyph.character == character) {
            return &glyph;
        }
    }
    return nullptr;
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

// text: Bounded null-terminated status text.
// left: Logical x coordinate of the first glyph.
// top: Logical y coordinate of the first glyph row.
// strip: Validated compact RGB565 target.
// first_row: Logical y origin of the compact target.
// first_column: Logical x origin of the compact target.
// stored_color: Color already encoded for the target byte order.
// Exception: Returns an unsupported-glyph or invalid-size error.
esp_err_t draw_text(const char *text, int left, int top, CODY2D::Surface strip,
                    uint16_t first_row, uint16_t first_column, uint16_t stored_color)
{
    const size_t length = std::strlen(text);
    if (length == 0 || length > 15) {
        ESP_LOGE(kTag, "Status text length rejected: %u (%s)",
                 static_cast<unsigned>(length), esp_err_to_name(ESP_ERR_INVALID_SIZE));
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
                if ((glyph->columns[column] & (1U << row)) != 0) {
                    put_pixel(strip, first_row, first_column,
                              left + static_cast<int>(index * kCharacterAdvance) + column,
                              top + row, stored_color);
                }
            }
        }
    }
    return ESP_OK;
}

// text: Bounded null-terminated status text to center on the display axis.
// top: Logical y coordinate of the first glyph row.
// strip: Validated compact RGB565 target.
// first_row: Logical y origin of the compact target.
// first_column: Logical x origin of the compact target.
// stored_color: Color already encoded for the target byte order.
// Exception: Returns the real bounded-text drawing error.
esp_err_t draw_centered_text(const char *text, int top, CODY2D::Surface strip,
                             uint16_t first_row, uint16_t first_column,
                             uint16_t stored_color)
{
    const size_t length = std::strlen(text);
    const int width = static_cast<int>(length * kCharacterAdvance - 1U);
    return draw_text(text, kPanelCenter - width / 2, top, strip,
                     first_row, first_column, stored_color);
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
    constexpr int width = 20;
    constexpr int height = 12;
    for (int x = 0; x < width; ++x) {
        put_pixel(strip, first_row, first_column, kBatteryLeft + x, kBatteryTop, stored_color);
        put_pixel(strip, first_row, first_column, kBatteryLeft + x,
                  kBatteryTop + height - 1, stored_color);
    }
    for (int y = 1; y < height - 1; ++y) {
        put_pixel(strip, first_row, first_column, kBatteryLeft, kBatteryTop + y, stored_color);
        put_pixel(strip, first_row, first_column, kBatteryLeft + width - 1,
                  kBatteryTop + y, stored_color);
    }
    for (int y = 4; y < 8; ++y) {
        put_pixel(strip, first_row, first_column, kBatteryLeft + width, kBatteryTop + y,
                  stored_color);
        put_pixel(strip, first_row, first_column, kBatteryLeft + width + 1,
                  kBatteryTop + y, stored_color);
    }
    if (overlay.battery_percent_valid) {
        const int fill_width = (overlay.battery_percent * (width - 4) + 99) / 100;
        for (int y = 3; y < height - 3; ++y) {
            for (int x = 0; x < fill_width; ++x) {
                put_pixel(strip, first_row, first_column, kBatteryLeft + 2 + x,
                          kBatteryTop + y, stored_color);
            }
        }
    }
    if (overlay.battery_charging) {
        const int bolt[][2] = {{10, 2}, {7, 7}, {10, 7}, {8, 11}, {14, 5}, {11, 5}};
        for (const auto &point : bolt) {
            put_pixel(strip, first_row, first_column, kBatteryLeft + point[0],
                      kBatteryTop + point[1], charge_color);
        }
    }
}

// bars: Number of truthful signal bands to render, from zero through three.
// strip: Validated compact RGB565 target.
// first_row: Logical y origin of the compact target.
// first_column: Logical x origin of the compact target.
// stored_color: Semantic Wi-Fi color encoded for the target byte order.
void draw_wifi(uint8_t bars, CODY2D::Surface strip, uint16_t first_row,
               uint16_t first_column, uint16_t stored_color)
{
    const uint8_t visible_bars = bars == 0 ? 3 : std::min<uint8_t>(bars, 3);
    for (int y = kWifiCenterY - 13; y <= kWifiCenterY; ++y) {
        for (int x = kWifiCenterX - 13; x <= kWifiCenterX + 13; ++x) {
            const int dx = x - kWifiCenterX;
            const int dy = y - kWifiCenterY;
            const int distance_squared = dx * dx + dy * dy;
            const bool dot = distance_squared <= 4;
            const bool inner = visible_bars >= 2 && distance_squared >= 30 &&
                distance_squared <= 49 && dy <= -3;
            const bool outer = visible_bars >= 3 && distance_squared >= 110 &&
                distance_squared <= 156 && dy <= -5;
            if ((visible_bars >= 1 && dot) || inner || outer) {
                put_pixel(strip, first_row, first_column, x, y, stored_color);
            }
        }
    }
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

// No parameters: return the complete half-open footprint shared by damage planning.
DamageRect status_overlay_damage_bounds()
{
    return {91, 5, 365, 64};
}

// observation: Coherent canonical battery, local calendar and Wi-Fi state.
// overlay: Receives a complete truthful presentation only after bounded formatting.
// Exception: Returns a formatting or snapshot-contract error without plausible fallback values.
esp_err_t prepare_status_overlay(const LUNASHELL::DeviceStatusObservation &observation,
                                 StatusOverlay &overlay)
{
    StatusOverlay next;
    const auto &battery = observation.battery;
    next.battery_tone = component_tone(battery);
    if (battery.data_stale) {
        std::snprintf(next.battery, sizeof(next.battery), "STALE");
    } else if (battery.health == LUNASHELL::DeviceHealth::Ready &&
               battery.last_error == ESP_OK && battery.data_valid) {
        if (!battery.battery_present) {
            std::snprintf(next.battery, sizeof(next.battery), "NO BATTERY");
            next.battery_tone = StatusTone::Muted;
        } else if (!battery.level_percent_valid || battery.level_percent < 0 ||
                   battery.level_percent > 100) {
            std::snprintf(next.battery, sizeof(next.battery), "UNAVAILABLE");
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
            next.battery_tone = battery.level_percent <= 12 ? StatusTone::Fault :
                battery.level_percent <= 30 ? StatusTone::Warning : StatusTone::Ready;
        }
        next.battery_charging = battery.battery_present && battery.usb_present &&
            std::strcmp(battery.power_state, "charging") == 0;
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
            next.time, sizeof(next.time), "%02u:%02u:%02u",
            static_cast<unsigned>(clock.local_minute / 60),
            static_cast<unsigned>(clock.local_minute % 60),
            static_cast<unsigned>(clock.local_second)
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
    if (wifi.health == LUNASHELL::DeviceHealth::Ready && wifi.last_error == ESP_OK &&
        wifi.data_valid) {
        if (!wifi.started || !wifi.credentials_configured) {
            next.wifi_tone = StatusTone::Muted;
        } else if (wifi.connected && wifi.ip_acquired) {
            next.wifi_tone = wifi.rssi < 0 && wifi.rssi <= -80
                ? StatusTone::Warning : StatusTone::Ready;
            next.wifi_bars = wifi.rssi >= -60 ? 3 : wifi.rssi >= -72 ? 2 : 1;
        } else if (wifi.connect_requested || wifi.reconnect_pending || wifi.connected) {
            next.wifi_tone = StatusTone::Warning;
        } else {
            next.wifi_tone = StatusTone::Muted;
        }
    }

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
        left.battery_percent == right.battery_percent &&
        left.wifi_bars == right.wifi_bars &&
        left.battery_percent_valid == right.battery_percent_valid &&
        left.battery_charging == right.battery_charging;
}

// overlay: Prepared immutable text and semantic icon state.
// opacity: Shared touch-chrome fade from transparent zero to opaque 255.
// strip: Compact RGB565 destination owned by the standalone renderer.
// first_row: Logical row represented by the strip's first pixel row.
// first_column: Logical column represented by the strip's first pixel.
// Exception: Returns the real surface, geometry or glyph error.
esp_err_t draw_status_overlay_rows(const StatusOverlay &overlay, uint8_t opacity,
                                   CODY2D::Surface strip, uint16_t first_row,
                                   uint16_t first_column)
{
    const esp_err_t surface_error = CODY2D::validate_surface(strip);
    if (surface_error != ESP_OK) {
        return surface_error;
    }
    if (first_column + strip.width > kPanelSize || first_row + strip.height > kPanelSize) {
        ESP_LOGE(kTag, "Status strip rejected: column=%u width=%u row=%u height=%u (%s)",
                 first_column, strip.width, first_row, strip.height,
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    if (opacity == 0) {
        return ESP_OK;
    }

    const uint16_t clock_color = CODY2D::rgb565_native_to_stored(
        CODY2D::fade_rgb565_to_black(tone_color(overlay.clock_tone), opacity),
        strip.byte_order
    );
    const uint16_t battery_color = CODY2D::rgb565_native_to_stored(
        CODY2D::fade_rgb565_to_black(tone_color(overlay.battery_tone), opacity),
        strip.byte_order
    );
    const uint16_t wifi_color = CODY2D::rgb565_native_to_stored(
        CODY2D::fade_rgb565_to_black(tone_color(overlay.wifi_tone), opacity),
        strip.byte_order
    );
    const uint16_t charge_color = CODY2D::rgb565_native_to_stored(
        CODY2D::fade_rgb565_to_black(kCharge, opacity), strip.byte_order
    );

    esp_err_t result = draw_centered_text(
        overlay.date, kDateTop, strip, first_row, first_column, clock_color
    );
    if (result == ESP_OK) {
        result = draw_centered_text(
            overlay.time, kTimeTop, strip, first_row, first_column, clock_color
        );
    }
    if (result != ESP_OK) {
        return result;
    }
    draw_battery(overlay, strip, first_row, first_column, battery_color, charge_color);
    result = draw_text(overlay.battery, kBatteryTextLeft, kBatteryTextTop,
                       strip, first_row, first_column, battery_color);
    if (result != ESP_OK) {
        return result;
    }
    draw_wifi(overlay.wifi_bars, strip, first_row, first_column, wifi_color);
    return ESP_OK;
}

}  // namespace CODY
