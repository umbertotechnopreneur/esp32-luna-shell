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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_BT_CONTROLLER_ENABLED
#include "esp_bt.h"
#endif

namespace LUNASHELL::radio_memory {

// Application startup budget, NOT a single allocation required by the SDK.
// Controller 51d9dfd allocates exchange memory in separate small blocks; the
// 0x7800 in the inspected EMI routine is an address offset, not a malloc size.
// Keep the same aggregate budget, with 8-KiB chunks covering the configured
// controller context and 4-KiB host stack separately. This is admission policy,
// not a guarantee about every vendor allocation; SDK results stay authoritative.
inline constexpr size_t kBleControllerDmaHoldBytes = 34 * 1024;
inline constexpr size_t kBleControllerDmaChunkBytes = 8 * 1024;
inline constexpr size_t kBleControllerDmaChunkCount =
    (kBleControllerDmaHoldBytes + kBleControllerDmaChunkBytes - 1) /
    kBleControllerDmaChunkBytes;
inline constexpr uint32_t kBleControllerDmaCaps =
    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;

struct HeapCapsDeleter {
    // allocation: A DMA chunk exclusively owned by this reservation.
    void operator()(uint8_t *allocation) const
    {
        heap_caps_free(allocation);
    }
};

class BleControllerDmaReservation final {
public:
    std::array<std::unique_ptr<uint8_t, HeapCapsDeleter>,
               kBleControllerDmaChunkCount> chunks{};

    // No parameters: only a fully populated reservation is admitted.
    explicit operator bool() const
    {
        for (const auto &chunk : chunks) {
            if (!chunk) {
                return false;
            }
        }
        return true;
    }

    // No parameters: release every chunk together at the controller handoff.
    void reset()
    {
        for (auto &chunk : chunks) {
            chunk.reset();
        }
    }
};

// reservation: Retains an existing lease or receives one until BLE controller startup.
// Exception: Returns the real allocation failure; never claims a partial lease.
inline esp_err_t reserveBleControllerDma(
    BleControllerDmaReservation &reservation
)
{
#if CONFIG_BT_CONTROLLER_ENABLED
    if (reservation) {
        return ESP_OK;
    }
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
        return ESP_OK;
    }

    // Build locally so failure frees all partial chunks without publishing a
    // successful budget. The radio lifecycle mutex serializes ownership.
    BleControllerDmaReservation pending;
    size_t remaining = kBleControllerDmaHoldBytes;
    for (auto &chunk : pending.chunks) {
        const size_t chunk_bytes = std::min(remaining, kBleControllerDmaChunkBytes);
        chunk.reset(static_cast<uint8_t *>(heap_caps_malloc(
            chunk_bytes, kBleControllerDmaCaps
        )));
        if (chunk) {
            remaining -= chunk_bytes;
            continue;
        }
        pending.reset();
        const size_t free_bytes =
            heap_caps_get_free_size(kBleControllerDmaCaps);
        const size_t largest_block =
            heap_caps_get_largest_free_block(kBleControllerDmaCaps);
        ESP_LOGE(
            "LUNASHELL_radio_mem",
            "BLE DMA budget reservation failed: "
            "free=%lu largest=%lu budget=%lu chunk=%lu (%s)",
            static_cast<unsigned long>(free_bytes),
            static_cast<unsigned long>(largest_block),
            static_cast<unsigned long>(kBleControllerDmaHoldBytes),
            static_cast<unsigned long>(chunk_bytes),
            esp_err_to_name(ESP_ERR_NO_MEM)
        );
        return ESP_ERR_NO_MEM;
    }

    reservation = std::move(pending);
    ESP_LOGI(
        "LUNASHELL_radio_mem",
        "BLE DMA startup budget held: %lu bytes in %lu chunks (max %lu)",
        static_cast<unsigned long>(kBleControllerDmaHoldBytes),
        static_cast<unsigned long>(kBleControllerDmaChunkCount),
        static_cast<unsigned long>(kBleControllerDmaChunkBytes)
    );
#else
    (void)reservation;
#endif
    return ESP_OK;
}

// reservation: Owned lease ceded only immediately before controller initialization.
// operation: Short lifecycle name recorded with the explicit handoff.
inline void releaseBleControllerDma(
    BleControllerDmaReservation &reservation,
    const char *operation
)
{
    if (!reservation) {
        return;
    }
    reservation.reset();
    ESP_LOGI(
        "LUNASHELL_radio_mem",
        "BLE controller DMA reservation released for %s",
        operation
    );
}

// operation: Radio lifecycle stage requesting SDK startup admission.
// Exception: Returns insufficient budget; passing is not proof of SDK success.
inline esp_err_t validateBleControllerDma(const char *operation)
{
#if CONFIG_BT_CONTROLLER_ENABLED
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
        return ESP_OK;
    }

    const size_t free_bytes = heap_caps_get_free_size(kBleControllerDmaCaps);
    const size_t largest_block =
        heap_caps_get_largest_free_block(kBleControllerDmaCaps);
    const size_t required_free = kBleControllerDmaHoldBytes;
    if (largest_block < kBleControllerDmaChunkBytes ||
        free_bytes < required_free) {
        ESP_LOGE(
            "LUNASHELL_radio_mem",
            "BLE controller start rejected for %s: DMA free=%lu "
            "required-free=%lu largest=%lu required-largest=%lu (%s)",
            operation,
            static_cast<unsigned long>(free_bytes),
            static_cast<unsigned long>(required_free),
            static_cast<unsigned long>(largest_block),
            static_cast<unsigned long>(kBleControllerDmaChunkBytes),
            esp_err_to_name(ESP_ERR_NO_MEM)
        );
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        "LUNASHELL_radio_mem",
        "BLE DMA budget admission for %s: free=%lu largest=%lu "
        "required-largest=%lu",
        operation,
        static_cast<unsigned long>(free_bytes),
        static_cast<unsigned long>(largest_block),
        static_cast<unsigned long>(kBleControllerDmaChunkBytes)
    );
#else
    (void)operation;
#endif
    return ESP_OK;
}

} // namespace LUNASHELL::radio_memory
