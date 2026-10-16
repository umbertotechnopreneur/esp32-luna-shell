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
constexpr uint16_t kPerformanceTop = 390;
constexpr uint16_t kNetworkTop = 402;
constexpr uint16_t kMemoryTop = 414;
constexpr uint16_t kStorageStateTop = 426;
constexpr uint16_t kStorageFreeTop = 438;
constexpr uint16_t kGlyphScale = 1;
constexpr uint16_t kGlyphColumns = 5;
constexpr uint16_t kGlyphRows = 7;
constexpr uint16_t kCharacterAdvance = (kGlyphColumns + 1) * kGlyphScale;
constexpr uint16_t kTextWhitesmoke = 0xF7BE; // RGB565 encoding of #F5F5F5.
constexpr uint16_t kTextBlack = 0x0000;

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
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x02, 0x04, 0x08, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
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
        std::snprintf(destination, capacity, "%s", processor.data_stale ? "STALE" : "UNAVAILABLE");
    }
}

// component: Published validity, health and original acquisition error.
bool current_component(const LUNASHELL::ComponentSnapshot &component)
{
    return component.health == LUNASHELL::DeviceHealth::Ready &&
           component.last_error == ESP_OK && component.data_valid && !component.data_stale;
}

// address: Fixed canonical IPv4 field; malformed or unterminated content is rejected.
bool valid_ipv4(const char (&address)[16])
{
    unsigned value = 0;
    unsigned digits = 0;
    unsigned octets = 0;
    for (size_t index = 0; index < sizeof(address); ++index) {
        const char character = address[index];
        if (character >= '0' && character <= '9') {
            value = value * 10 + static_cast<unsigned>(character - '0');
            ++digits;
            if (digits > 3 || value > 255) {
                return false;
            }
            continue;
        }
        if (digits == 0) {
            return false;
        }
        if (character == '\0') {
            return octets == 3;
        }
        if (character != '.' || octets >= 3) {
            return false;
        }
        ++octets;
        digits = 0;
        value = 0;
    }
    return false;
}

// observation: One canonical diagnostics projection with owner-provided validity.
// readout: Receives IP, free memory and storage lines without new acquisition.
// Exception: Rejects malformed IPv4 or inconsistent current storage capacities with ESP_ERR_INVALID_RESPONSE.
esp_err_t format_device_details(const LUNASHELL::DiagnosticsObservation &observation,
                               DebugReadout &readout)
{
    const LUNASHELL::WifiSnapshot &wifi = observation.wifi;
    if (current_component(wifi) && wifi.connected && wifi.ip_acquired) {
        if (!valid_ipv4(wifi.ip_address)) {
            ESP_LOGE(kTag, "Canonical Wi-Fi IP rejected: malformed IPv4 (%s)",
                     esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
            return ESP_ERR_INVALID_RESPONSE;
        }
        std::snprintf(readout.network, sizeof(readout.network), "IP %s", wifi.ip_address);
    } else if (wifi.data_stale) {
        std::snprintf(readout.network, sizeof(readout.network), "IP STALE");
    } else if (current_component(wifi) && !wifi.ip_acquired) {
        std::snprintf(readout.network, sizeof(readout.network), "IP NO IP");
    }

    const LUNASHELL::MemorySnapshot &memory = observation.memory;
    if (current_component(memory)) {
        std::snprintf(readout.memory, sizeof(readout.memory), "RAM FREE %luK PSRAM %luK",
                      static_cast<unsigned long>(memory.internal_free_bytes / 1024),
                      static_cast<unsigned long>(memory.psram_free_bytes / 1024));
    } else if (memory.data_stale) {
        std::snprintf(readout.memory, sizeof(readout.memory), "RAM STALE PSRAM STALE");
    }

    const LUNASHELL::StorageSnapshot &storage = observation.storage;
    const bool accessible = current_component(storage) && storage.mounted &&
        storage.card_response_verified && !storage.poisoned &&
        storage.cleanup_complete && storage.cleanup_error == ESP_OK &&
        storage.mount_outcome == LUNASHELL::StorageMountOutcome::Mounted;
    if (storage.data_stale) {
        std::snprintf(readout.storage_state, sizeof(readout.storage_state),
                      "SD STALE ACCESS UNAVAILABLE");
        std::snprintf(readout.storage_free, sizeof(readout.storage_free), "SD FREE STALE");
    } else if (accessible) {
        const bool coherent_space = storage.capacity_bytes > 0 &&
            storage.filesystem_total_bytes > 0 &&
            storage.filesystem_total_bytes <= storage.capacity_bytes &&
            storage.free_bytes <= storage.filesystem_total_bytes;
        if (!coherent_space) {
            ESP_LOGE(kTag, "Canonical SD space rejected: inconsistent capacities (%s)",
                     esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
            return ESP_ERR_INVALID_RESPONSE;
        }
        std::snprintf(readout.storage_state, sizeof(readout.storage_state), "SD PRESENT ACCESS OK");
        std::snprintf(readout.storage_free, sizeof(readout.storage_free), "SD FREE %lluMIB",
                      static_cast<unsigned long long>(storage.free_bytes / (1024ULL * 1024ULL)));
    } else if (storage.card_response_verified) {
        // A verified card response proves presence even when FAT is inaccessible.
        std::snprintf(readout.storage_state, sizeof(readout.storage_state), "SD PRESENT NO ACCESS");
    }
    // No card response is inconclusive on a board with no card-detect signal.
    return ESP_OK;
}

// text: Bounded, null-terminated diagnostic text, already prepared for this frame.
// top: First text row below the automatic greeting title.
// strip: Valid compact destination tile with its own width and stride.
// first_row: Logical origin used to clip glyphs across strip boundaries.
// first_column: Logical x origin used to clip glyphs without moving their layout.
// stored_color: Text color already encoded for the strip byte order.
esp_err_t draw_line_rows(const char *text, uint16_t top,
                        CODY2D::Surface strip, uint16_t first_row, uint16_t first_column,
                        uint16_t stored_color)
{
    const uint16_t bottom = top + kGlyphRows * kGlyphScale;
    if (first_row >= bottom || first_row + strip.height <= top) {
        return ESP_OK;
    }
    const size_t length = std::strlen(text);
    if (length == 0 || length > 60) {
        ESP_LOGE(kTag, "Diagnostic text length rejected: %u", static_cast<unsigned>(length));
        return ESP_ERR_INVALID_SIZE;
    }
    const int width = length * kCharacterAdvance - kGlyphScale;
    const int left = (kPanelSize - width) / 2;
    const int half_width = kPanelCenter - left;
    const int lowest_offset = bottom - 1 - kPanelCenter;
    // At this bottom edge the chord is narrowest. Its corners bound all rows.
    if (top < kPerformanceTop || left < 0 ||
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
                        const int canvas_x = glyph_left + column * kGlyphScale + scale_x;
                        if (canvas_x < first_column || canvas_x >= first_column + strip.width) {
                            continue;
                        }
                        strip.pixels[destination_y * strip.stride + canvas_x - first_column] =
                            stored_color;
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
    return {0, kPerformanceTop, kPanelSize,
        static_cast<uint16_t>(kStorageFreeTop + kGlyphRows * kGlyphScale)};
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
// readout: One immutable diagnostic text block for the current complete redraw.
// Exception: Returns original canonical read errors or rejects inconsistent current values.
esp_err_t prepare_debug_readout(const FrameRateMeasurement &measurement,
                               int64_t now_us, DebugReadout &readout)
{
    LUNASHELL::DiagnosticsObservation observation;
    const esp_err_t snapshot_error =
        LUNASHELL::DeviceSnapshotService::instance().readDiagnosticsObservation(observation);
    if (snapshot_error != ESP_OK) {
        ESP_LOGE(kTag, "Canonical diagnostics observation failed (%s)", esp_err_to_name(snapshot_error));
        return snapshot_error;
    }
    DebugReadout next;
    const int64_t measurement_age_us = now_us - measurement.measured_at_us;
    if (measurement.available && measurement_age_us >= 0 && measurement_age_us <= kReadoutFreshnessUs) {
        std::snprintf(next.frame_rate, sizeof(next.frame_rate), "FPS %lu.%lu",
                      static_cast<unsigned long>(measurement.frames_per_second_tenths / 10),
                      static_cast<unsigned long>(measurement.frames_per_second_tenths % 10));
    }
    char cpu1[12];
    char cpu2[12];
    format_processor(observation.processor, ProcessorId::Cpu1, cpu1, sizeof(cpu1));
    format_processor(observation.processor, ProcessorId::Cpu2, cpu2, sizeof(cpu2));
    std::snprintf(next.processors, sizeof(next.processors), "CPU1 %s CPU2 %s", cpu1, cpu2);
    const esp_err_t details_error = format_device_details(observation, next);
    if (details_error != ESP_OK) {
        return details_error;
    }
    readout = next;
    return ESP_OK;
}

// readout: Compact display-owned heap projection, without acquisition or snapshot schema growth.
// Exception: Logs and returns original snapshot errors or incoherent current heap values.
esp_err_t prepare_memory_diagnostics(MemoryDiagnosticReadout &readout)
{
    readout = {};
    LUNASHELL::DiagnosticsObservation observation;
    const esp_err_t error =
        LUNASHELL::DeviceSnapshotService::instance().readDiagnosticsObservation(observation);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Memory diagnostics snapshot read failed (%s)", esp_err_to_name(error));
        return error;
    }
    const auto &memory = observation.memory;
    if (!current_component(memory)) {
        readout.internal.stale = memory.data_stale;
        readout.psram.stale = memory.data_stale;
        return ESP_OK;
    }
    if (memory.internal_heap_total_bytes == 0 || memory.psram_heap_total_bytes == 0 ||
        memory.internal_free_bytes > memory.internal_heap_total_bytes ||
        memory.internal_largest_block_bytes > memory.internal_free_bytes ||
        memory.internal_minimum_free_bytes > memory.internal_free_bytes ||
        memory.psram_free_bytes > memory.psram_heap_total_bytes ||
        memory.psram_largest_block_bytes > memory.psram_free_bytes ||
        memory.psram_minimum_free_bytes > memory.psram_free_bytes) {
        ESP_LOGE(kTag, "Memory diagnostics capacities rejected (%s)",
            esp_err_to_name(ESP_ERR_INVALID_RESPONSE));
        return ESP_ERR_INVALID_RESPONSE;
    }
    readout.internal = {memory.internal_heap_total_bytes, memory.internal_free_bytes,
        memory.internal_largest_block_bytes, memory.internal_minimum_free_bytes,
        memory.internal_free_blocks, true, false};
    readout.psram = {memory.psram_heap_total_bytes, memory.psram_free_bytes,
        memory.psram_largest_block_bytes, memory.psram_minimum_free_bytes,
        memory.psram_free_blocks, true, false};
    return ESP_OK;
}

// readout: Shared prepared text, with unavailable measurements visibly labelled.
// strip: Destination owned by the companion renderer; no font or GUI allocation occurs.
// first_row: Logical strip origin, mapped through the same cardinal rotation as Cody.
// first_column: Logical x origin of the compact strip, leaving text positions unchanged.
// background_rgb565: Authored scene background, white or dark on current scenes.
esp_err_t draw_debug_readout_rows(const DebugReadout &readout,
                                CODY2D::Surface strip, uint16_t first_row,
                                uint16_t first_column, uint16_t background_rgb565)
{
    const esp_err_t surface_error = CODY2D::validate_surface(strip);
    if (surface_error != ESP_OK) {
        return surface_error;
    }
    if (first_column + strip.width > kPanelSize || first_row + strip.height > kPanelSize) {
        ESP_LOGE(kTag, "Diagnostic strip rejected: column=%u width=%u row=%u height=%u (%s)",
                 first_column, strip.width, first_row, strip.height,
                 esp_err_to_name(ESP_ERR_INVALID_SIZE));
        return ESP_ERR_INVALID_SIZE;
    }
    const uint16_t text_color = background_rgb565 == 0xFFFF ? kTextBlack : kTextWhitesmoke;
    const uint16_t stored_text_color = CODY2D::rgb565_native_to_stored(text_color, strip.byte_order);
    char performance[60];
    std::snprintf(performance, sizeof(performance), "%s %s", readout.frame_rate, readout.processors);
    struct TextLine {
        const char *text;
        uint16_t top;
    };
    const TextLine lines[] = {
        {performance, kPerformanceTop},
        {readout.network, kNetworkTop},
        {readout.memory, kMemoryTop},
        {readout.storage_state, kStorageStateTop},
        {readout.storage_free, kStorageFreeTop},
    };
    for (const TextLine &line : lines) {
        const esp_err_t draw_error = draw_line_rows(
            line.text, line.top, strip, first_row, first_column, stored_text_color
        );
        if (draw_error != ESP_OK) {
            return draw_error;
        }
    }
    return ESP_OK;
}

}  // namespace CODY
