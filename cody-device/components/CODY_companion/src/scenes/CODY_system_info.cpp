#include "CODY_system_info.hpp"
#include "CODY_title_style.hpp"
#include "CODY_typography.hpp"
#include "CODY_theme_generated.hpp"
#include "CODY_ui_components.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include "CODY2D_controls.hpp"
#include "CODY2D_paths.hpp"
#include "CODY_page_marks.hpp"
#include "CODY_vibeware_logo.hpp"
#include "LUNASHELL_build_metadata.hpp"
#include "LUNASHELL_device_snapshot.hpp"
#include "esp_log.h"

namespace CODY {
namespace {

using View = SystemInfoPresentation;
using State = View::ValueState;
constexpr char kTag[] = "CODY_system_info";
constexpr DamageRect kBounds{73, 73, 393, 400};
constexpr uint16_t kWhite = UI::Palette::text;
constexpr uint16_t kMuted = UI::Palette::muted;
constexpr uint16_t kCyan = UI::Palette::cyan;
constexpr uint16_t kIndigo = Theme::dashboard_secondary;
constexpr uint16_t kGreen = Theme::dashboard_positive;
constexpr uint16_t kTrack = Theme::dashboard_track;
constexpr uint16_t kDivider = Theme::dashboard_separator;

// operation: Failed native preparation or raster operation.
// error: Original error, logged without changing its identity.
esp_err_t result(const char *operation, esp_err_t error)
{
    if (error != ESP_OK) { ESP_LOGE(kTag, "%s failed (%s)", operation, esp_err_to_name(error)); }
    return error;
}

// observation: Aged canonical component with original acquisition status.
// read_error: Original failure to obtain the coherent projection, if any.
View::Status status_of(const LUNASHELL::ComponentSnapshot &observation, esp_err_t read_error)
{
    if (read_error != ESP_OK) { return {State::Error, read_error}; }
    if (observation.health == LUNASHELL::DeviceHealth::Unknown && !observation.data_valid &&
        observation.last_attempt_at_us == 0) {
        return {State::Unavailable, ESP_OK};
    }
    if (observation.last_error != ESP_OK) { return {State::Error, observation.last_error}; }
    if (observation.data_stale) { return {State::Stale, ESP_OK}; }
    return {observation.data_valid && observation.health == LUNASHELL::DeviceHealth::Ready
        ? State::Current : State::Unavailable, ESP_OK};
}

// left: Retained measurement availability and error identity.
// right: Newly projected measurement availability and error identity.
bool same_status(View::Status left, View::Status right)
{
    return left.state == right.state && left.error == right.error;
}

// left: Retained visual values; acquisition timestamps intentionally do not cause repaint.
// right: New visual values for the selected page.
bool same_values(const View::Values &left, const View::Values &right)
{
    if (left.page != right.page || left.build != right.build || left.image_version != right.image_version ||
        left.uptime_seconds != right.uptime_seconds ||
        left.previous_uptime_seconds != right.previous_uptime_seconds ||
        left.previous_uptime_valid != right.previous_uptime_valid) { return false; }
    for (size_t index = 0; index < left.cpus.size(); ++index) {
        if (!same_status(left.cpus[index].status, right.cpus[index].status) ||
            left.cpus[index].percent != right.cpus[index].percent) { return false; }
        const auto &a = left.heaps[index];
        const auto &b = right.heaps[index];
        if (!same_status(a.status, b.status) || a.total != b.total || a.free != b.free || a.largest != b.largest) {
            return false;
        }
    }
    return true;
}

// input: Borrowed model data, projected only for the selected information page.
// values: Small normalized visual projection; invalid samples never retain numeric values.
void project_values(const SystemInfoInput &input, View::Values &values)
{
    values = {};
    values.page = input.page;
    if (input.page == InfoPage::Firmware) {
        values.build = input.build;
        values.image_version = input.image_version;
        return;
    }
    if (input.page == InfoPage::VibeWare) return;
    if (input.page == InfoPage::System) {
        values.uptime_seconds = input.uptime_seconds;
        values.previous_uptime_seconds = input.previous_uptime_valid ? input.previous_uptime_seconds : 0;
        values.previous_uptime_valid = input.previous_uptime_valid;
    }
    if (input.diagnostics == nullptr) {
        const View::Status status{input.diagnostics_error == ESP_OK ? State::Unavailable : State::Error,
            input.diagnostics_error};
        for (auto &cpu : values.cpus) { cpu.status = status; }
        for (auto &heap : values.heaps) { heap.status = status; }
        return;
    }
    if (input.page == InfoPage::System) {
        const auto &processor = input.diagnostics->processor;
        const auto source = status_of(processor, input.diagnostics_error);
        for (size_t index = 0; index < values.cpus.size(); ++index) {
            auto &cpu = values.cpus[index];
            cpu.status = source;
            if (source.state != State::Current) { continue; }
            const auto &core = processor.cores[index];
            if (processor.core_count == 0 || processor.core_count > values.cpus.size()) {
                cpu.status = {State::Invalid, ESP_ERR_INVALID_RESPONSE};
            } else if (index >= processor.core_count || !core.available || !core.utilization_available) {
                cpu.status = {State::Unavailable, ESP_OK};
            } else if (core.utilization_percent > 100) {
                cpu.status = {State::Invalid, ESP_ERR_INVALID_RESPONSE};
            } else {
                cpu.percent = core.utilization_percent;
            }
        }
    }
    const auto &memory = input.diagnostics->memory;
    const auto source = status_of(memory, input.diagnostics_error);
    values.heaps[0] = {source, memory.internal_heap_total_bytes,
        memory.internal_free_bytes, memory.internal_largest_block_bytes};
    values.heaps[1] = {source, memory.psram_heap_total_bytes,
        memory.psram_free_bytes, memory.psram_largest_block_bytes};
    for (auto &heap : values.heaps) {
        if (source.state == State::Current &&
            (heap.total == 0 || heap.free > heap.total || heap.largest > heap.free)) {
            heap.status = {heap.total == 0 ? State::Unavailable : State::Invalid, ESP_ERR_INVALID_RESPONSE};
        }
        if (heap.status.state != State::Current) { heap.total = heap.free = heap.largest = 0; }
    }
}

// view: Fixed glyph owner for the current page.
// font: Shared immutable native glyphs.
// text: Complete bounded printable ASCII caption.
// bounds: Admitted central rectangle used for centering.
// color: Native RGB565 foreground.
// Exception: Preserves invalid geometry, capacity or native glyph errors.
esp_err_t add_caption(View &view, const CODY2D::MonoFont &font, const char *text,
    CODY2D::Rect bounds, uint16_t color)
{
    const size_t length = std::strlen(text);
    if (view.caption_count >= view.captions.size() || length == 0 ||
        length >= sizeof(view.captions[0].text) || font.height > bounds.height ||
        bounds.x < kBounds.left || bounds.y < kBounds.top ||
        bounds.x + bounds.width > kBounds.right || bounds.y + bounds.height > kBounds.bottom) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint32_t width = 0;
    auto error = CODY2D::measure_text_width(font, {text, static_cast<uint16_t>(length)}, width);
    if (error != ESP_OK) { return error; }
    if (width > bounds.width) { return ESP_ERR_INVALID_SIZE; }
    auto &caption = view.captions[view.caption_count];
    std::memcpy(caption.text, text, length + 1);
    error = CODY2D::prepare_text_line(font, {caption.text, static_cast<uint16_t>(length)},
        {static_cast<int16_t>(bounds.x + (bounds.width - width) / 2),
         static_cast<int16_t>(bounds.y + (bounds.height - font.height) / 2)}, color, caption.line);
    if (error == ESP_OK) { ++view.caption_count; }
    return error;
}

// view: Fixed presentation owner.
// text: Bounded centered detail caption.
// top: First row of its 25-pixel slot.
// color: Native caption color.
// Exception: Preserves the original text preparation error.
esp_err_t add_detail(View &view, const char *text, uint16_t top, uint16_t color = kMuted)
{
    return add_caption(view, *view.body_font, text, {73, top, 320, 25}, color);
}

// view: Retained presentation receiving an ordinary label.
// text: Complete label rather than a measurement or status.
// bounds: Existing admitted label slot.
// color: Original semantic ink color.
// Exception: Returns the original font or caption preparation failure.
esp_err_t add_system_label(View &view, const char *text, CODY2D::Rect bounds, uint16_t color)
{
    const CODY2D::MonoFont *font = nullptr;
    const auto error = Typography::label_font(text, bounds, *view.body_font, font);
    return error == ESP_OK ? add_caption(view, *font, text, bounds, color) : error;
}

// view: Fixed bar owner.
// bounds: Nonempty admitted rectangle.
// top: Native gradient color at its first row.
// bottom: Native gradient color at its last row.
// Exception: Rejects exhausted storage or invalid geometry.
esp_err_t add_bar(View &view, CODY2D::Rect bounds, uint16_t top, uint16_t bottom)
{
    if (view.bar_count >= view.bars.size() || bounds.width == 0 || bounds.height == 0 ||
        bounds.x < kBounds.left || bounds.y < kBounds.top ||
        bounds.x + bounds.width > kBounds.right || bounds.y + bounds.height > kBounds.bottom) {
        return ESP_ERR_INVALID_SIZE;
    }
    view.bars[view.bar_count++] = {bounds, top, bottom};
    return ESP_OK;
}

// status: Original unavailable, stale or failed measurement state.
// text: Receives one bounded explanation instead of a fabricated numeric value.
void format_status(View::Status status, char (&text)[32])
{
    if (status.state == State::Error) {
        std::snprintf(text, sizeof(text), "Error 0x%lx",
            static_cast<unsigned long>(static_cast<uint32_t>(status.error)));
    } else {
        std::snprintf(text, sizeof(text), "%s", status.state == State::Stale ? "Stale" :
            status.state == State::Invalid ? "Invalid data" : "Unavailable");
    }
}

// seconds: Current or retained last-observed monotonic uptime.
// text: Receives exact whole seconds, grouped into day and clock fields.
void format_uptime(uint32_t seconds, char (&text)[32])
{
    const unsigned long days = seconds / 86400U;
    const unsigned hours = seconds % 86400U / 3600U;
    const unsigned minutes = seconds % 3600U / 60U;
    const unsigned remainder = seconds % 60U;
    if (days != 0) { std::snprintf(text, sizeof(text), "%lud %02u:%02u:%02u", days, hours, minutes, remainder); }
    else { std::snprintf(text, sizeof(text), "%02u:%02u:%02u", hours, minutes, remainder); }
}

// view: Page presentation with independent canonical core and heap measurements.
// Exception: Preserves native caption or bar geometry errors.
esp_err_t prepare_system(View &view)
{
    char text[32]{};
    for (size_t index = 0; index < view.values.cpus.size(); ++index) {
        const auto &cpu = view.values.cpus[index];
        const uint16_t offset = static_cast<uint16_t>(index * 50);
        std::snprintf(text, sizeof(text), "CPU %u", static_cast<unsigned>(index + 1));
        auto error = add_system_label(view, text, {93, static_cast<uint16_t>(137 + offset), 100, 25}, kMuted);
        if (error != ESP_OK) { return error; }
        if (cpu.status.state == State::Current) {
            std::snprintf(text, sizeof(text), "%u%%", static_cast<unsigned>(cpu.percent));
            error = add_caption(view, Typography::monospace_large(), text, {260, static_cast<uint16_t>(129 + offset), 113, 40}, kWhite);
            if (error == ESP_OK) { error = add_bar(view, {93, static_cast<uint16_t>(169 + offset), 280, 8}, kTrack, kTrack); }
            const auto width = static_cast<uint16_t>(280U * cpu.percent / 100U);
            if (error == ESP_OK && width != 0) {
                error = add_bar(view, {93, static_cast<uint16_t>(169 + offset), width, 8}, kCyan, kIndigo);
            }
        } else {
            format_status(cpu.status, text);
            error = add_caption(view, *view.body_font, text, {205, static_cast<uint16_t>(129 + offset), 188, 40}, kMuted);
        }
        if (error != ESP_OK) { return error; }
    }
    esp_err_t error = ESP_OK;
    for (size_t index = 0; index < view.values.heaps.size(); ++index) {
        const auto &heap = view.values.heaps[index];
        const char *name = index == 0 ? "RAM" : "PSRAM";
        if (heap.status.state == State::Current) {
            std::snprintf(text, sizeof(text), "%s %lu/%lu KiB", name,
                static_cast<unsigned long>((heap.total - heap.free) / 1024),
                static_cast<unsigned long>(heap.total / 1024));
        } else {
            char status[32]{};
            format_status(heap.status, status);
            std::snprintf(text, sizeof(text), "%s %.24s", name, status);
        }
        error = add_detail(view, text, static_cast<uint16_t>(230U + index * 26U),
            heap.status.state == State::Current ? kCyan : kMuted);
        if (error != ESP_OK) return error;
    }
    error = add_system_label(view, "UPTIME", {73, 284, 320, 25}, kMuted);
    format_uptime(view.values.uptime_seconds, text);
    if (error == ESP_OK) { error = add_detail(view, text, 309, kWhite); }
    if (error == ESP_OK) { error = add_detail(view, "PREVIOUS: LAST OBSERVED", 337); }
    if (view.values.previous_uptime_valid) { format_uptime(view.values.previous_uptime_seconds, text); }
    else { std::strcpy(text, "Unavailable"); }
    if (error == ESP_OK) { error = add_detail(view, text, 362, kWhite); }
    return error;
}

// view: Brand-only page containing the website address below the original logo.
// Exception: Preserves the original caption preparation error.
esp_err_t prepare_vibeware(View &view)
{
    return add_detail(view, "umbertogiacobbi.biz", 355, kWhite);
}

// view: Page presentation with independent internal RAM and PSRAM heap capacities.
// Exception: Preserves native text or bar errors; invalid heaps have no bar or counters.
esp_err_t prepare_memory(View &view)
{
    char text[32]{};
    for (size_t index = 0; index < view.values.heaps.size(); ++index) {
        const auto &heap = view.values.heaps[index];
        const uint16_t top = static_cast<uint16_t>(134 + index * 134);
        auto error = add_system_label(view, index == 0 ? "INTERNAL RAM" : "PSRAM",
            {73, top, 320, 25}, kWhite);
        if (error != ESP_OK) { return error; }
        if (heap.status.state != State::Current) {
            format_status(heap.status, text);
            error = add_detail(view, text, static_cast<uint16_t>(top + 47));
            if (error != ESP_OK) { return error; }
            continue;
        }
        const uint32_t used = heap.total - heap.free;
        std::snprintf(text, sizeof(text), "%lu / %lu KiB USED",
            static_cast<unsigned long>(used / 1024), static_cast<unsigned long>(heap.total / 1024));
        error = add_detail(view, text, static_cast<uint16_t>(top + 27), kCyan);
        // These segments partition allocator capacity, not physical memory addresses.
        const auto used_width = static_cast<uint16_t>(uint64_t{used} * 280 / heap.total);
        const auto largest_end = static_cast<uint16_t>((uint64_t{used} + heap.largest) * 280 / heap.total);
        if (error == ESP_OK) { error = add_bar(view, {93, static_cast<uint16_t>(top + 59), 280, 10}, kTrack, kTrack); }
        if (error == ESP_OK && used_width != 0) {
            error = add_bar(view, {93, static_cast<uint16_t>(top + 59), used_width, 10}, kCyan, kIndigo);
        }
        if (error == ESP_OK && largest_end > used_width) {
            error = add_bar(view, {static_cast<uint16_t>(93 + used_width), static_cast<uint16_t>(top + 59),
                static_cast<uint16_t>(largest_end - used_width), 10}, kGreen, kGreen);
        }
        std::snprintf(text, sizeof(text), "FREE %lu KiB", static_cast<unsigned long>(heap.free / 1024));
        if (error == ESP_OK) { error = add_detail(view, text, static_cast<uint16_t>(top + 72)); }
        std::snprintf(text, sizeof(text), "LARGEST FREE %lu KiB", static_cast<unsigned long>(heap.largest / 1024));
        if (error == ESP_OK) { error = add_detail(view, text, static_cast<uint16_t>(top + 97), kGreen); }
        if (error != ESP_OK) { return error; }
    }
    return ESP_OK;
}

// state: Immutable build-field acquisition outcome.
const char *build_status(LUNASHELL::FirmwareBuildFieldState state)
{
    using BuildState = LUNASHELL::FirmwareBuildFieldState;
    switch (state) {
    case BuildState::Error: return "Error";
    case BuildState::Unsupported: return "Unsupported";
    default: return "Unavailable";
    }
}

// source: Immutable bounded build field, or null when unavailable.
// capacity: Its maximum readable byte count.
// font: Actual glyph metrics for the central 320-pixel row.
// text: Receives the complete value or an explicit ellipsis for long metadata.
// Exception: Preserves native measurement errors or impossible text geometry.
esp_err_t format_metadata(const char *source, size_t capacity, const CODY2D::MonoFont &font, char (&text)[32])
{
    if (source == nullptr || capacity == 0 || source[0] == '\0') {
        std::strcpy(text, "Unavailable");
        return ESP_OK;
    }
    const auto *end = static_cast<const char *>(std::memchr(source, '\0', capacity));
    if (end == nullptr || !std::all_of(source, end, [](char value) { return value >= 32 && value <= 126; })) {
        std::strcpy(text, "Invalid data");
        return ESP_OK;
    }
    const size_t length = static_cast<size_t>(end - source);
    size_t visible = std::min(length, sizeof(text) - 1);
    bool shortened = visible < length;
    for (;;) {
        const size_t prefix = shortened ? visible - 3 : visible;
        std::memcpy(text, source, prefix);
        if (shortened) { std::memcpy(text + prefix, "...", 3); }
        text[visible] = '\0';
        uint32_t width = 0;
        const auto error = CODY2D::measure_text_width(font, {text, static_cast<uint16_t>(visible)}, width);
        if (error != ESP_OK) { return error; }
        if (width <= 320) { return ESP_OK; }
        if (visible <= 3) { return ESP_ERR_INVALID_SIZE; }
        --visible;
        shortened = true;
    }
}

// view: Page presentation borrowing only immutable running-image metadata.
// Exception: Preserves original metadata formatting or caption errors.
esp_err_t prepare_firmware(View &view)
{
    const auto *build = view.values.build;
    using BuildState = LUNASHELL::FirmwareBuildFieldState;
    const bool clock_available = build != nullptr && build->clock_state == BuildState::Available;
    const bool host_available = build != nullptr && build->computer_state == BuildState::Available;
    const char *clock_state = build == nullptr ? "Unavailable" : build_status(build->clock_state);
    const char *host_state = build == nullptr ? "Unavailable" : build_status(build->computer_state);
    const char *sources[]{view.values.image_version,
        clock_available ? build->local_date_time : clock_state,
        clock_available ? build->time_zone : clock_state,
        host_available ? build->computer : host_state};
    const size_t capacities[]{32, clock_available ? sizeof(build->local_date_time) : std::strlen(clock_state) + 1,
        clock_available ? sizeof(build->time_zone) : std::strlen(clock_state) + 1,
        host_available ? sizeof(build->computer) : std::strlen(host_state) + 1};
    constexpr const char *labels[]{"VERSION", "BUILT", "TIME ZONE", "BUILD HOST"};
    char text[32]{};
    for (size_t index = 0; index < 4; ++index) {
        const auto top = static_cast<uint16_t>(135 + index * 62);
        auto error = add_system_label(view, labels[index], {73, top, 320, 25}, kMuted);
        if (error == ESP_OK) { error = format_metadata(sources[index], capacities[index], *view.body_font, text); }
        if (error == ESP_OK) { error = add_detail(view, text, static_cast<uint16_t>(top + 25), kWhite); }
        if (error != ESP_OK) { return error; }
    }
    return ESP_OK;
}

// bar: Prepared capacity or decoration rectangle.
// strip: Valid borrowed RGB565 tile.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
void draw_bar(const View::Bar &bar, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const auto &bounds = bar.bounds;
    const int left = std::max<int>(first_column, bounds.x);
    const int right = std::min<int>(first_column + strip.width, bounds.x + bounds.width);
    const int top = std::max<int>(first_row, bounds.y);
    const int bottom = std::min<int>(first_row + strip.height, bounds.y + bounds.height);
    if (left >= right) { return; }
    for (int y = top; y < bottom; ++y) {
        const auto weight = static_cast<uint8_t>(bar.top == bar.bottom || bounds.height == 1
            ? 0 : (y - bounds.y) * 255 / (bounds.height - 1));
        const auto color = CODY2D::rgb565_native_to_stored(CODY2D::blend_rgb565(bar.bottom, bar.top, weight), strip.byte_order);
        auto *row = strip.pixels + (y - first_row) * strip.stride + left - first_column;
        std::fill_n(row, right - left, color);
    }
}

// view: Page identity and admitted heading icon rectangle.
// strip: Valid borrowed RGB565 tile.
// first_row: Logical y origin of the tile.
// first_column: Logical x origin of the tile.
// Exception: Preserves native stroke or semantic icon raster failures.
esp_err_t draw_icon(const View &view, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    const auto &bounds = view.icon_bounds;
    if (first_row >= bounds.y + bounds.height || first_row + strip.height <= bounds.y ||
        first_column >= bounds.x + bounds.width || first_column + strip.width <= bounds.x) { return ESP_OK; }
    if (view.values.page == InfoPage::Firmware) {
        return CODY2D::draw_action_icon_rows(CODY2D::ActionIcon::Gear, bounds, kCyan, strip, first_row, first_column);
    }
    struct Segment { uint8_t x1, y1, x2, y2; };
    // Authored 24-pixel outlines reuse the native antialiased stroke raster.
    static constexpr Segment cpu[]{
        {6,6,18,6}, {18,6,18,18}, {18,18,6,18}, {6,18,6,6},
        {9,9,15,9}, {15,9,15,15}, {15,15,9,15}, {9,15,9,9},
        {9,2,9,6}, {15,2,15,6}, {9,18,9,22}, {15,18,15,22},
        {2,9,6,9}, {2,15,6,15}, {18,9,22,9}, {18,15,22,15}};
    static constexpr Segment memory[]{
        {3,6,21,6}, {21,6,21,17}, {21,17,3,17}, {3,17,3,6},
        {7,9,7,14}, {12,9,12,14}, {17,9,17,14},
        {6,17,6,21}, {10,17,10,21}, {14,17,14,21}, {18,17,18,21}};
    const auto *segments = view.values.page == InfoPage::System ? cpu : memory;
    const size_t count = view.values.page == InfoPage::System ? sizeof(cpu) / sizeof(cpu[0]) : sizeof(memory) / sizeof(memory[0]);
    CODY2D::PreparedStroke stroke;
    for (size_t index = 0; index < count; ++index) {
        const auto &line = segments[index];
        auto error = CODY2D::prepare_stroke(
            {bounds.x + line.x1 * bounds.width / 24.0F, bounds.y + line.y1 * bounds.height / 24.0F},
            {bounds.x + line.x2 * bounds.width / 24.0F, bounds.y + line.y2 * bounds.height / 24.0F},
            1.8F, kCyan, stroke);
        if (error == ESP_OK) { error = CODY2D::stroke_prepared_rows(strip, first_row, stroke, first_column); }
        if (error != ESP_OK) { return error; }
    }
    return ESP_OK;
}

// strip: Valid borrowed RGB565 tile.
// first_row: Logical y origin of this tile.
// first_column: Logical x origin of this tile.
// Exception: Rejects a malformed flash row or palette index.
esp_err_t draw_vibeware_logo(CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    constexpr int left = 113;
    constexpr int top = 105;
    const int start_y = std::max<int>(first_row, top);
    const int end_y = std::min<int>(first_row + strip.height, top + VibeWareLogo::size);
    const int start_x = std::max<int>(first_column, left);
    const int end_x = std::min<int>(first_column + strip.width, left + VibeWareLogo::size);
    if (start_x >= end_x || start_y >= end_y) return ESP_OK;
    for (int y = start_y; y < end_y; ++y) {
        const auto row = static_cast<size_t>(y - top);
        const size_t begin = VibeWareLogo::offsets[row];
        const size_t end = VibeWareLogo::offsets[row + 1U];
        if (end < begin || end > sizeof(VibeWareLogo::runs) || (end - begin) % 2U != 0) return ESP_ERR_INVALID_SIZE;
        int x = left;
        for (size_t cursor = begin; cursor < end; cursor += 2U) {
            const uint8_t count = VibeWareLogo::runs[cursor];
            const uint8_t index = VibeWareLogo::runs[cursor + 1U];
            if (count == 0 || index >= sizeof(VibeWareLogo::palette) / sizeof(VibeWareLogo::palette[0]) ||
                x + count > left + VibeWareLogo::size) return ESP_ERR_INVALID_RESPONSE;
            const int clipped_left = std::max(x, start_x);
            const int clipped_right = std::min(x + count, end_x);
            if (clipped_left < clipped_right) {
                const auto color = CODY2D::rgb565_native_to_stored(VibeWareLogo::palette[index], strip.byte_order);
                auto *pixels = strip.pixels + (y - first_row) * strip.stride + clipped_left - first_column;
                std::fill_n(pixels, clipped_right - clipped_left, color);
            }
            x += count;
        }
        if (x != left + VibeWareLogo::size) return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

// state: Immutable presentation retained through synchronous frame retirement.
// strip: Borrowed tile with its original stride and byte order.
// first_row: Logical tile y origin.
// first_column: Logical tile x origin.
// Exception: Preserves native surface, icon and glyph raster errors.
esp_err_t draw_rows(const void *state, CODY2D::Surface strip, uint16_t first_row, uint16_t first_column)
{
    if (state == nullptr) { return result("draw state", ESP_ERR_INVALID_ARG); }
    const auto &view = *static_cast<const View *>(state);
    if (!view.valid) { return result("draw state", ESP_ERR_INVALID_STATE); }
    auto error = CODY2D::validate_surface(strip);
    if (error != ESP_OK) { return result("draw tile", error); }
    for (size_t index = 0; index < view.bar_count; ++index) { draw_bar(view.bars[index], strip, first_row, first_column); }
    error = view.values.page == InfoPage::VibeWare
        ? draw_vibeware_logo(strip, first_row, first_column)
        : draw_icon(view, strip, first_row, first_column);
    for (size_t index = 0; error == ESP_OK && index < view.caption_count; ++index) {
        const auto &line = view.captions[index].line;
        error = index == 0 && view.values.page != InfoPage::VibeWare ? ESP_OK :
            CODY2D::draw_text_rows(line, line.text.length, strip, first_row, first_column);
    }
    if (error == ESP_OK) {
        error = draw_page_marks(static_cast<uint8_t>(view.values.page), 5, strip, first_row, first_column, 396);
    }
    return result("draw rows", error);
}

// view: Prepared persistent page owner.
// content: Receives only immutable callback data and its presentation revision.
esp_err_t attach_content(const View &view, SceneContent &content)
{
    content = {};
    content.bounds = kBounds;
    content.state = &view;
    content.revision = view.revision;
    content.draw_rows = draw_rows;
    if (view.values.page == InfoPage::VibeWare) return ESP_OK;
    const auto &line = view.captions[0].line;
    return prepare_styled_title(line.text,
        {static_cast<uint16_t>(view.icon_bounds.x + 38), 79, line.bounds.width, 46}, content.heading, 3, true);
}

} // namespace

// page: Retained child page, never wrapped into an unrelated widget.
// step: Explicit previous/next intent.
bool step_info_page(InfoPage &page, WidgetStep step)
{
    if (step == WidgetStep::Previous) {
        switch (page) {
        case InfoPage::ProjectLink: page = InfoPage::VibeWare; return true;
        case InfoPage::VibeWare: page = InfoPage::Firmware; return true;
        case InfoPage::Firmware: page = InfoPage::Memory; return true;
        case InfoPage::Memory: page = InfoPage::System; return true;
        case InfoPage::System: return false;
        }
    } else if (step == WidgetStep::Next) {
        switch (page) {
        case InfoPage::System: page = InfoPage::Memory; return true;
        case InfoPage::Memory: page = InfoPage::Firmware; return true;
        case InfoPage::Firmware: page = InfoPage::VibeWare; return true;
        case InfoPage::VibeWare: page = InfoPage::ProjectLink; return true;
        case InfoPage::ProjectLink: return false;
        }
    }
    return false;
}

// input: Canonical model observations and selected About child route.
// title_font: Native heading and percentage glyphs.
// body_font: Native details and page index glyphs.
// presentation: Persistent owner, reusable when its visible values have not changed.
// content: Receives the central information content only.
// Exception: Preserves invalid page identity, revision overflow and preparation errors.
esp_err_t prepare_system_info(const SystemInfoInput &input,
    const CODY2D::MonoFont &title_font, const CODY2D::MonoFont &body_font,
    View &presentation, SceneContent &content)
{
    if (input.page > InfoPage::VibeWare) { return result("page identity", ESP_ERR_INVALID_ARG); }
    View::Values values{};
    project_values(input, values);
    if (presentation.valid && same_values(presentation.values, values) &&
        presentation.title_font == &title_font && presentation.body_font == &body_font) {
        return attach_content(presentation, content);
    }
    if (presentation.revision == std::numeric_limits<uint32_t>::max()) {
        return result("revision", ESP_ERR_INVALID_STATE);
    }
    presentation.valid = false;
    presentation.caption_count = presentation.bar_count = 0;
    presentation.values = values;
    presentation.title_font = &title_font;
    presentation.body_font = &body_font;
    if (input.page == InfoPage::VibeWare) {
        const auto error = prepare_vibeware(presentation);
        if (error != ESP_OK) return result("prepare brand", error);
        ++presentation.revision;
        presentation.valid = true;
        return attach_content(presentation, content);
    }
    const char *title = input.page == InfoPage::System ? "SYSTEM" : input.page == InfoPage::Memory ? "MEMORY" : "FIRMWARE";
    const auto length = std::strlen(title);
    auto &heading = presentation.captions[0];
    if (length == 0 || length >= sizeof(heading.text)) return result("heading storage", ESP_ERR_INVALID_SIZE);
    const auto width = static_cast<uint16_t>(std::min<uint32_t>(
        TitleStyleDetail::width(static_cast<uint16_t>(length), 3), 222));
    const auto group_left = static_cast<uint16_t>(233 - (width + 38U) / 2U);
    presentation.icon_bounds = {group_left, 88, 28, 28};
    std::memcpy(heading.text, title, length + 1U);
    heading.line.text = {heading.text, static_cast<uint16_t>(length)};
    heading.line.bounds = {static_cast<uint16_t>(group_left + 38U), 79, width, 46};
    presentation.caption_count = 1;
    auto error = ESP_OK;
    if (error == ESP_OK) { error = add_bar(presentation, {94, 125, 278, 2}, kDivider, kDivider); }
    if (error == ESP_OK) {
        switch (input.page) {
        case InfoPage::System: error = prepare_system(presentation); break;
        case InfoPage::Memory: error = prepare_memory(presentation); break;
        case InfoPage::Firmware: error = prepare_firmware(presentation); break;
        case InfoPage::VibeWare: return result("page identity", ESP_ERR_INVALID_ARG);
        case InfoPage::ProjectLink: return result("page identity", ESP_ERR_INVALID_ARG);
        }
    }
    if (error != ESP_OK) { return result("prepare page", error); }
    ++presentation.revision;
    presentation.valid = true;
    return attach_content(presentation, content);
}

} // namespace CODY
