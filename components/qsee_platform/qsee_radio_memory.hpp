#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_BT_CONTROLLER_ENABLED
#include "esp_bt.h"
#endif

namespace qsee::radio_memory {

// ESP32-S3 controller build 51d9dfd requests one 0x7800-byte contiguous
// internal DMA block while initializing its exchange memory. Keep that block
// intact while Wi-Fi allocates its long-lived DMA buffers; otherwise the
// controller asserts internally instead of returning ESP_ERR_NO_MEM.
inline constexpr size_t kBleControllerDmaReservationBytes = 0x7800;
inline constexpr size_t kBleControllerDmaHeadroomBytes = 4096;
inline constexpr uint32_t kBleControllerDmaCaps =
    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;

struct HeapCapsDeleter {
    void operator()(uint8_t *allocation) const
    {
        heap_caps_free(allocation);
    }
};

using BleControllerDmaReservation =
    std::unique_ptr<uint8_t, HeapCapsDeleter>;

inline esp_err_t reserveBleControllerDma(
    BleControllerDmaReservation &reservation
)
{
#if CONFIG_BT_CONTROLLER_ENABLED
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
        return ESP_OK;
    }

    auto *allocation = static_cast<uint8_t *>(heap_caps_malloc(
        kBleControllerDmaReservationBytes,
        kBleControllerDmaCaps
    ));
    if (allocation == nullptr) {
        const size_t free_bytes =
            heap_caps_get_free_size(kBleControllerDmaCaps);
        const size_t largest_block =
            heap_caps_get_largest_free_block(kBleControllerDmaCaps);
        ESP_LOGE(
            "qsee_radio_mem",
            "BLE controller DMA reservation failed before Wi-Fi: "
            "free=%lu largest=%lu required=%lu (%s)",
            static_cast<unsigned long>(free_bytes),
            static_cast<unsigned long>(largest_block),
            static_cast<unsigned long>(kBleControllerDmaReservationBytes),
            esp_err_to_name(ESP_ERR_NO_MEM)
        );
        return ESP_ERR_NO_MEM;
    }

    reservation.reset(allocation);
    ESP_LOGI(
        "qsee_radio_mem",
        "BLE controller DMA reservation held across Wi-Fi startup: %lu bytes",
        static_cast<unsigned long>(kBleControllerDmaReservationBytes)
    );
#else
    (void)reservation;
#endif
    return ESP_OK;
}

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
        "qsee_radio_mem",
        "BLE controller DMA reservation released for %s",
        operation
    );
}

inline esp_err_t validateBleControllerDma(const char *operation)
{
#if CONFIG_BT_CONTROLLER_ENABLED
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
        return ESP_OK;
    }

    const size_t free_bytes = heap_caps_get_free_size(kBleControllerDmaCaps);
    const size_t largest_block =
        heap_caps_get_largest_free_block(kBleControllerDmaCaps);
    const size_t required_free =
        kBleControllerDmaReservationBytes + kBleControllerDmaHeadroomBytes;
    if (largest_block < kBleControllerDmaReservationBytes ||
        free_bytes < required_free) {
        ESP_LOGE(
            "qsee_radio_mem",
            "BLE controller start rejected for %s: DMA free=%lu "
            "required-free=%lu largest=%lu required-largest=%lu (%s)",
            operation,
            static_cast<unsigned long>(free_bytes),
            static_cast<unsigned long>(required_free),
            static_cast<unsigned long>(largest_block),
            static_cast<unsigned long>(kBleControllerDmaReservationBytes),
            esp_err_to_name(ESP_ERR_NO_MEM)
        );
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        "qsee_radio_mem",
        "BLE controller DMA preflight for %s: free=%lu largest=%lu "
        "required-largest=%lu",
        operation,
        static_cast<unsigned long>(free_bytes),
        static_cast<unsigned long>(largest_block),
        static_cast<unsigned long>(kBleControllerDmaReservationBytes)
    );
#else
    (void)operation;
#endif
    return ESP_OK;
}

} // namespace qsee::radio_memory
