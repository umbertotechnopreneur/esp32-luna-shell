#include "CODY_debug_hud.hpp"

#include <cstdio>
#include <cstring>

#include "LUNASHELL_device_snapshot.hpp"
#include "esp_log.h"

namespace CODY {
namespace {

constexpr char kTag[] = "CODY_debug_hud";
constexpr int64_t kMeasurementWindowUs = 1000000;
constexpr int64_t kReadoutFreshnessUs = 2000000;
constexpr uint16_t kPanelSize = 466;
constexpr uint16_t kPanelCenter = 233;
constexpr uint16_t kFrameRateTop = 398;
constexpr uint16_t kProcessorTop = 419;
constexpr uint16_t kGlyphScale = 2;
constexpr uint16_t kGlyphColumns = 5;
constexpr uint16_t kGlyphRows = 7;
constexpr uint16_t kCharacterAdvance = (kGlyphColumns + 1) * kGlyphScale;
constexpr uint16_t kTextWhite = 0xFFFF;

enum class ProcessorId : uint8_t {
    Cpu1,
    Cpu2,
};

struct MonoGlyph {
    char character;
    uint8_t columns[kGlyphColumns];
};

// Only the diagnostic alphabet lives in flash. No GUI or font engine is needed.
constexpr MonoGlyph kGlyphs[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'%', {0x63, 0x13, 0x08, 0x64, 0x63}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
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
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'N', {0x7F, 0x02, 0x04, 0x08, 0x7F}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
};

// character: One of the bounded diagnostic alphabet's ASCII characters.
const MonoGlyph *find_glyph(char character)
{
    for (const MonoGlyph &glyph : kGlyphs) {
        if (glyph.character == character) {
            return &glyph;
        }
    }
    return nullptr;
}

// processor: Coherent published state with its component validity and real error.
// identity: Display CPU1 maps to SDK core 0; CPU2 maps to SDK core 1.
// destination: Receives a percentage only when its measured source is current.
// capacity: Writable destination size, including its terminating null.
void format_processor(const LUNASHELL::ProcessorSnapshot &processor,
                      ProcessorId identity, char *destination, size_t capacity)
{
    const uint8_t sdk_core = identity == ProcessorId::Cpu1 ? 0 : 1;
    const LUNASHELL::CoreSnapshot &core = processor.cores[sdk_core];
    const bool current = processor.health == LUNASHELL::DeviceHealth::Ready &&
                         processor.last_error == ESP_OK && processor.data_valid &&
                         !processor.data_stale && processor.core_count > sdk_core &&
                         core.available && core.utilization_available &&
                         core.utilization_percent <= 100;
    if (current) {
        std::snprintf(destination, capacity, "%u%%",
                      static_cast<unsigned>(core.utilization_percent));
    } else {
        std::snprintf(destination, capacity, "N/A");
    }
}

// text: Bounded, null-terminated diagnostic text, already prepared for this frame.
// top: First text row, entirely below the [73,393) central safe square.
// strip: Valid destination tile spanning the logical screen width.
// first_row: Logical origin used to clip glyphs across strip boundaries.
esp_err_t draw_line_rows(const char *text, uint16_t top,
                        CODY2D::Surface strip, uint16_t first_row)
{
    const uint16_t bottom = top + kGlyphRows * kGlyphScale;
    if (first_row >= bottom || first_row + strip.height <= top) {
        return ESP_OK;
    }
    const size_t length = std::strlen(text);
    if (length == 0 || length > 32) {
        ESP_LOGE(kTag, "Diagnostic text length rejected: %u", static_cast<unsigned>(length));
        return ESP_ERR_INVALID_SIZE;
    }
    const int width = length * kCharacterAdvance - kGlyphScale;
    const int left = (kPanelSize - width) / 2;
    const int half_width = kPanelCenter - left;
    const int lowest_offset = bottom - 1 - kPanelCenter;
    // At this bottom edge the chord is narrowest. Its corners bound all rows.
    if (top < 393 || left < 0 ||
        half_width * half_width + lowest_offset * lowest_offset >= kPanelCenter * kPanelCenter) {
        ESP_LOGE(kTag, "Diagnostic text exceeds circular layout: width=%d top=%u", width, top);
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t character_index = 0; character_index < length; ++character_index) {
        const MonoGlyph *glyph = find_glyph(text[character_index]);
        if (glyph == nullptr) {
            ESP_LOGE(kTag, "Diagnostic glyph unavailable: ASCII=%u",
                     static_cast<unsigned>(static_cast<unsigned char>(text[character_index])));
            return ESP_ERR_NOT_SUPPORTED;
        }
        const int glyph_left = left + character_index * kCharacterAdvance;
        for (uint16_t column = 0; column < kGlyphColumns; ++column) {
            for (uint16_t row = 0; row < kGlyphRows; ++row) {
                if ((glyph->columns[column] & (1U << row)) == 0) {
                    continue;
                }
                for (uint16_t scale_y = 0; scale_y < kGlyphScale; ++scale_y) {
                    const int destination_y = top + row * kGlyphScale + scale_y - first_row;
                    if (destination_y < 0 || destination_y >= strip.height) {
                        continue;
                    }
                    for (uint16_t scale_x = 0; scale_x < kGlyphScale; ++scale_x) {
                        const int destination_x = glyph_left + column * kGlyphScale + scale_x;
                        strip.pixels[destination_y * strip.stride + destination_x] = kTextWhite;
                    }
                }
            }
        }
    }
    return ESP_OK;
}

}  // namespace

// No parameters: keep invalidation tied to the same layout constants as drawing.
DamageRect debug_readout_damage_bounds()
{
    return {0, kFrameRateTop, kPanelSize,
        static_cast<uint16_t>(kProcessorTop + kGlyphRows * kGlyphScale)};
}

// measurement: Counters for successfully completed frames, reset on route entry/resume.
// started_at_us: Start of this successfully completed frame.
// completed_at_us: Real panel completion; a failed frame never reaches this function.
void record_frame_rate(FrameRateMeasurement &measurement,
                       int64_t started_at_us, int64_t completed_at_us)
{
    if (measurement.window_started_us == 0) {
        measurement.window_started_us = started_at_us;
    }
    ++measurement.completed_frames;
    const int64_t elapsed_us = completed_at_us - measurement.window_started_us;
    if (elapsed_us < kMeasurementWindowUs) {
        return;
    }
    measurement.frames_per_second_tenths = static_cast<uint32_t>(
        (measurement.completed_frames * 10000000ULL + elapsed_us / 2) / elapsed_us
    );
    measurement.measured_at_us = completed_at_us;
    measurement.available = true;
    measurement.completed_frames = 0;
    measurement.window_started_us = completed_at_us;
}

// measurement: Completed-frame timing, never the requested 33 ms schedule.
// now_us: Observation time for FPS freshness.
// readout: One immutable text pair for the current complete redraw.
esp_err_t prepare_debug_readout(const FrameRateMeasurement &measurement,
                               int64_t now_us, DebugReadout &readout)
{
    LUNASHELL::ProcessorSnapshot processor;
    const esp_err_t snapshot_error =
        LUNASHELL::DeviceSnapshotService::instance().readProcessorObservation(processor);
    if (snapshot_error != ESP_OK) {
        ESP_LOGE(kTag, "Canonical processor observation failed (%s)", esp_err_to_name(snapshot_error));
        return snapshot_error;
    }
    DebugReadout next;
    const int64_t measurement_age_us = now_us - measurement.measured_at_us;
    if (measurement.available && measurement_age_us >= 0 && measurement_age_us <= kReadoutFreshnessUs) {
        std::snprintf(next.frame_rate, sizeof(next.frame_rate), "FPS %lu.%lu",
                      static_cast<unsigned long>(measurement.frames_per_second_tenths / 10),
                      static_cast<unsigned long>(measurement.frames_per_second_tenths % 10));
    }
    char cpu1[8];
    char cpu2[8];
    format_processor(processor, ProcessorId::Cpu1, cpu1, sizeof(cpu1));
    format_processor(processor, ProcessorId::Cpu2, cpu2, sizeof(cpu2));
    std::snprintf(next.processors, sizeof(next.processors), "CPU1 %s CPU2 %s", cpu1, cpu2);
    readout = next;
    return ESP_OK;
}

// readout: Shared prepared text, with unavailable measurements visibly labelled N/A.
// strip: Destination owned by the companion renderer; no font or GUI allocation occurs.
// first_row: Logical strip origin, mapped through the same cardinal rotation as Cody.
esp_err_t draw_debug_readout_rows(const DebugReadout &readout,
                                CODY2D::Surface strip, uint16_t first_row)
{
    const esp_err_t surface_error = CODY2D::validate_surface(strip);
    if (surface_error != ESP_OK) {
        return surface_error;
    }
    if (strip.width != kPanelSize || first_row >= kPanelSize ||
        first_row + strip.height > kPanelSize) {
        ESP_LOGE(kTag, "Diagnostic strip rejected: width=%u row=%u height=%u",
                 strip.width, first_row, strip.height);
        return ESP_ERR_INVALID_SIZE;
    }
    const esp_err_t fps_error = draw_line_rows(readout.frame_rate, kFrameRateTop, strip, first_row);
    if (fps_error != ESP_OK) {
        return fps_error;
    }
    return draw_line_rows(readout.processors, kProcessorTop, strip, first_row);
}

}  // namespace CODY
