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

#pragma once

#include <cmath>

#include "LUNASHELL_device_snapshot.hpp"

namespace LUNASHELL::snapshot_semantics {

inline bool component_ready(const ComponentSnapshot &component)
{
    return component.health == DeviceHealth::Ready &&
           component.last_error == ESP_OK &&
           component.data_valid &&
           !component.data_stale;
}

inline bool gps_fix_valid(const GpsSnapshot &gps)
{
    return gps.has_fix &&
           gps.utc_valid &&
           gps.fix_quality > 0 &&
           gps.satellites > 0 &&
           std::isfinite(gps.latitude_degrees) &&
           gps.latitude_degrees >= -90.0 &&
           gps.latitude_degrees <= 90.0 &&
           std::isfinite(gps.longitude_degrees) &&
           gps.longitude_degrees >= -180.0 &&
           gps.longitude_degrees <= 180.0 &&
           std::isfinite(gps.altitude_m) &&
           std::isfinite(gps.hdop) &&
           gps.hdop >= 0.0F &&
           std::isfinite(gps.speed_mps) &&
           gps.speed_mps >= 0.0F &&
           std::isfinite(gps.course_degrees) &&
           gps.course_degrees >= 0.0F &&
           gps.course_degrees <= 360.0F;
}

inline bool storage_cleanup_failed(const StorageSnapshot &storage)
{
    return storage.poisoned ||
           !storage.cleanup_complete ||
           storage.cleanup_error != ESP_OK;
}

inline bool storage_format_required(const StorageSnapshot &storage)
{
    return storage.format_required &&
           storage.mount_outcome == StorageMountOutcome::FormatRequired &&
           storage.card_response_verified &&
           storage.cleanup_complete &&
           storage.cleanup_error == ESP_OK &&
           !storage.poisoned &&
           !storage.mounted;
}

inline bool storage_mounted_ready(const StorageSnapshot &storage)
{
    return component_ready(storage) &&
           storage.mount_outcome == StorageMountOutcome::Mounted &&
           storage.mounted &&
           storage.card_response_verified &&
           !storage_cleanup_failed(storage);
}

}  // namespace LUNASHELL::snapshot_semantics
