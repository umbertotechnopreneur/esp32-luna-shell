#include "sdkconfig.h"
#include "bsp_display_dma.h"

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_log.h"

// The display lock serializes all leases, including the emergency row.
static const char *TAG = "BSP_display_dma";
static bool display_dma_low_memory_reported;
static bool display_dma_emergency_in_use;
static uint32_t display_dma_dual_slot_count;
#if !CONFIG_LUNASHELL_BUILD_ENABLE_UI
static uint32_t display_dma_dual_slot_failure_count;
#endif
static uint32_t display_dma_single_slot_count;
static uint32_t display_dma_reduced_rows_count;
static uint32_t display_dma_emergency_count;
// A small lifeboat, not a permanent multi-row reservation.
static DMA_ATTR uint8_t display_dma_emergency_buffer[BSP_DISPLAY_DMA_EMERGENCY_BYTES]
    __attribute__((aligned(BSP_DISPLAY_DMA_ALIGNMENT)));

// Keep the lifeboat check with its owner; callers never borrow the raw buffer.
esp_err_t bsp_display_validate_emergency_buffer(void)
{
    if (!esp_ptr_internal(display_dma_emergency_buffer) ||
        !esp_ptr_dma_capable(display_dma_emergency_buffer)) {
        ESP_LOGE(TAG, "Emergency row is not internal DMA-capable: %p (%s)",
                 display_dma_emergency_buffer, esp_err_to_name(ESP_ERR_INVALID_STATE));
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

// byte_count: Bounded allocation size rounded up to the DMA alignment.
static size_t bsp_display_dma_aligned_size(size_t byte_count)
{
    return
        (byte_count + BSP_DISPLAY_DMA_ALIGNMENT - 1U) &
        ~(BSP_DISPLAY_DMA_ALIGNMENT - 1U);
}

// count: Failure or emergency count, logged at powers of two to avoid a log storm.
bool bsp_display_should_log_count(uint32_t count)
{
    return count == 1U || (count & (count - 1U)) == 0U;
}

// lease: Owned pipeline released only after all submitted transfers complete.
void bsp_display_release_dma_pipeline(
    bsp_display_dma_lease_t *lease
)
{
    if (lease == NULL || lease->data == NULL) {
        return;
    }
    if (lease->heap_owned) {
        if (lease->second_data != NULL) {
            heap_caps_free(lease->second_data);
        }
        heap_caps_free(lease->data);
    } else if (lease->emergency) {
        display_dma_emergency_in_use = false;
    }
    *lease = (bsp_display_dma_lease_t){0};
}

// lease: Owned pipeline, whose optional second native slot may be noncontiguous.
// slot: Zero-based storage traversal index, bounded by the actual allocation.
uint8_t *bsp_display_dma_slot(const bsp_display_dma_lease_t *lease, size_t slot)
{
    if (lease == NULL || lease->data == NULL || slot >= lease->buffer_count) {
        return NULL;
    }
    if (slot == 1U && lease->second_data != NULL) {
        return lease->second_data;
    }
    return lease->data + slot * lease->slot_stride_bytes;
}

// UI flushes and standalone frames may request two transient slots.
// source_row_bytes: Nonzero RGB565 row length, bounded by the physical panel.
// maximum_transfer_rows: Maximum rows in one slot, bounded by the panel transfer limit.
// preferred_buffer_count: One or two independently aligned pipeline slots.
// out_lease: Receives ownership only after a successful allocation.
esp_err_t bsp_display_acquire_dma_pipeline(
    size_t source_row_bytes,
    size_t maximum_transfer_rows,
    size_t preferred_buffer_count,
    bsp_display_dma_lease_t *out_lease
)
{
    if (source_row_bytes == 0U || source_row_bytes > BSP_DISPLAY_DMA_ROW_BYTES ||
        maximum_transfer_rows == 0U || maximum_transfer_rows > BSP_DISPLAY_STANDALONE_DMA_TRANSFER_HEIGHT ||
        preferred_buffer_count == 0U ||
        preferred_buffer_count > BSP_DISPLAY_DMA_PIPELINE_DEPTH ||
        out_lease == NULL) {
        ESP_LOGE(TAG, "Display DMA allocation rejected: invalid pipeline request");
        return ESP_ERR_INVALID_ARG;
    }

    *out_lease = (bsp_display_dma_lease_t){0};
    const esp_err_t emergency_error = bsp_display_validate_emergency_buffer();
    if (emergency_error != ESP_OK) {
        return emergency_error;
    }


    const uint32_t capabilities =
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const size_t full_slot_stride_bytes = bsp_display_dma_aligned_size(
        source_row_bytes * maximum_transfer_rows
    );
    const size_t full_pipeline_bytes =
        full_slot_stride_bytes * preferred_buffer_count;
    size_t largest_block =
        heap_caps_get_largest_free_block(capabilities);
    bool attempted_full_slot = false;

    if (largest_block >=
        full_pipeline_bytes + BSP_DISPLAY_DMA_HEADROOM_BYTES) {
        attempted_full_slot = preferred_buffer_count == 1U;
        uint8_t *pipeline = heap_caps_aligned_alloc(
            BSP_DISPLAY_DMA_ALIGNMENT,
            full_pipeline_bytes,
            capabilities
        );
        if (pipeline != NULL) {
            *out_lease = (bsp_display_dma_lease_t){
                .data = pipeline,
                .slot_stride_bytes = full_slot_stride_bytes,
                .buffer_count = preferred_buffer_count,
                .transfer_rows = maximum_transfer_rows,
                .heap_owned = true,
            };
            if (preferred_buffer_count > 1U) {
                ++display_dma_dual_slot_count;
            } else {
                ++display_dma_single_slot_count;
            }
            return ESP_OK;
        }
        ESP_LOGW(
            TAG,
            "Allocating the %lu-byte preferred DMA pipeline failed despite "
            "a %lu-byte largest block",
            (unsigned long)full_pipeline_bytes,
            (unsigned long)largest_block
        );
    }

#if !CONFIG_LUNASHELL_BUILD_ENABLE_UI
    // Native frames need independent DMA slots, not one contiguous frame buffer.
    // BLE/startup fragmentation must not force synchronous rendering when two
    // smaller internal blocks fit with the same aggregate headroom and budget.
    // Preserve overlap with shorter paired-row slots before degrading to one slot.
    if (preferred_buffer_count == BSP_DISPLAY_DMA_PIPELINE_DEPTH) {
        size_t paired_rows = maximum_transfer_rows & ~(size_t)1U;
        for (; paired_rows >= 2U; paired_rows -= 2U) {
            const size_t paired_stride = bsp_display_dma_aligned_size(source_row_bytes * paired_rows);
            largest_block = heap_caps_get_largest_free_block(capabilities);
            if (largest_block < paired_stride + BSP_DISPLAY_DMA_HEADROOM_BYTES ||
                heap_caps_get_free_size(capabilities) <
                    paired_stride * 2U + BSP_DISPLAY_DMA_HEADROOM_BYTES) {
                continue;
            }
            uint8_t *first_slot = heap_caps_aligned_alloc(
                BSP_DISPLAY_DMA_ALIGNMENT, paired_stride, capabilities);
            if (first_slot == NULL) {
                continue;
            }
            largest_block = heap_caps_get_largest_free_block(capabilities);
            uint8_t *second_slot = NULL;
            // Headroom is aggregate, not duplicated in each fragmented block.
            if (largest_block >= paired_stride && heap_caps_get_free_size(capabilities) >=
                paired_stride + BSP_DISPLAY_DMA_HEADROOM_BYTES) {
                second_slot = heap_caps_aligned_alloc(
                    BSP_DISPLAY_DMA_ALIGNMENT, paired_stride, capabilities);
            }
            if (second_slot == NULL) {
                heap_caps_free(first_slot);
                continue;
            }
            *out_lease = (bsp_display_dma_lease_t){
                .data = first_slot,
                .second_data = second_slot,
                .slot_stride_bytes = paired_stride,
                .buffer_count = BSP_DISPLAY_DMA_PIPELINE_DEPTH,
                .transfer_rows = paired_rows,
                .heap_owned = true,
            };
            ++display_dma_dual_slot_count;
            if (paired_rows != maximum_transfer_rows && !display_dma_low_memory_reported) {
                ESP_LOGW(TAG, "Native two-slot DMA uses reduced rows=%lu slot=%lu; aggregate headroom=%u",
                    (unsigned long)paired_rows, (unsigned long)paired_stride,
                    (unsigned int)BSP_DISPLAY_DMA_HEADROOM_BYTES);
                display_dma_low_memory_reported = true;
            }
            return ESP_OK;
        }
        ++display_dma_dual_slot_failure_count;
        if (bsp_display_should_log_count(display_dma_dual_slot_failure_count)) {
            ESP_LOGW(TAG, "Native two-slot DMA unavailable: count=%lu largest=%lu; trying a single slot",
                (unsigned long)display_dma_dual_slot_failure_count, (unsigned long)largest_block);
        }
    }
#endif

    if (preferred_buffer_count > 1U) {
        largest_block = heap_caps_get_largest_free_block(capabilities);
    }
    if (!attempted_full_slot &&
        largest_block >=
        full_slot_stride_bytes + BSP_DISPLAY_DMA_HEADROOM_BYTES) {
        attempted_full_slot = true;
        uint8_t *single_slot = heap_caps_aligned_alloc(
            BSP_DISPLAY_DMA_ALIGNMENT,
            full_slot_stride_bytes,
            capabilities
        );
        if (single_slot != NULL) {
            *out_lease = (bsp_display_dma_lease_t){
                .data = single_slot,
                .slot_stride_bytes = full_slot_stride_bytes,
                .buffer_count = 1U,
                .transfer_rows = maximum_transfer_rows,
                .heap_owned = true,
            };
            ++display_dma_single_slot_count;
            return ESP_OK;
        }
        ESP_LOGW(
            TAG,
            "Allocating the %lu-byte single-slot DMA pipeline failed despite "
            "a %lu-byte largest block",
            (unsigned long)full_slot_stride_bytes,
            (unsigned long)largest_block
        );
    }

    largest_block = heap_caps_get_largest_free_block(capabilities);
    const size_t usable_block =
        largest_block > BSP_DISPLAY_DMA_HEADROOM_BYTES
            ? largest_block - BSP_DISPLAY_DMA_HEADROOM_BYTES
            : 0U;
    size_t fallback_rows = usable_block / source_row_bytes;
    if (fallback_rows > maximum_transfer_rows) {
        fallback_rows = maximum_transfer_rows;
    }
    if (attempted_full_slot && fallback_rows == maximum_transfer_rows) {
        --fallback_rows;
    }

    while (fallback_rows > BSP_DISPLAY_DMA_EMERGENCY_ROWS) {
        const size_t fallback_slot_stride_bytes =
            bsp_display_dma_aligned_size(source_row_bytes * fallback_rows);
        if (fallback_slot_stride_bytes + BSP_DISPLAY_DMA_HEADROOM_BYTES <=
            largest_block) {
            uint8_t *fallback = heap_caps_aligned_alloc(
                BSP_DISPLAY_DMA_ALIGNMENT,
                fallback_slot_stride_bytes,
                capabilities
            );
            if (fallback != NULL) {
                if (!display_dma_low_memory_reported) {
                    ESP_LOGW(
                        TAG,
                        "Display DMA low-memory pipeline active: rows=%lu "
                        "slot=%lu largest=%lu",
                        (unsigned long)fallback_rows,
                        (unsigned long)fallback_slot_stride_bytes,
                        (unsigned long)largest_block
                    );
                    display_dma_low_memory_reported = true;
                }
                *out_lease = (bsp_display_dma_lease_t){
                    .data = fallback,
                    .slot_stride_bytes = fallback_slot_stride_bytes,
                    .buffer_count = 1U,
                    .transfer_rows = fallback_rows,
                    .heap_owned = true,
                };
                ++display_dma_reduced_rows_count;
                return ESP_OK;
            }
        }
        --fallback_rows;
    }

    if (source_row_bytes <= BSP_DISPLAY_DMA_EMERGENCY_BYTES &&
        !display_dma_emergency_in_use) {
        display_dma_emergency_in_use = true;
        ++display_dma_emergency_count;
        *out_lease = (bsp_display_dma_lease_t){
            .data = display_dma_emergency_buffer,
            .slot_stride_bytes = BSP_DISPLAY_DMA_EMERGENCY_BYTES,
            .buffer_count = 1U,
            .transfer_rows = maximum_transfer_rows < BSP_DISPLAY_DMA_EMERGENCY_ROWS
                ? maximum_transfer_rows : BSP_DISPLAY_DMA_EMERGENCY_ROWS,
            .heap_owned = false,
            .emergency = true,
        };
        if (bsp_display_should_log_count(display_dma_emergency_count)) {
            ESP_LOGW(
                TAG,
                "Display DMA emergency row active: count=%lu row=%lu "
                "free=%lu minimum=%lu largest=%lu headroom=%u",
                (unsigned long)display_dma_emergency_count,
                (unsigned long)source_row_bytes,
                (unsigned long)heap_caps_get_free_size(capabilities),
                (unsigned long)heap_caps_get_minimum_free_size(capabilities),
                (unsigned long)largest_block,
                (unsigned int)BSP_DISPLAY_DMA_HEADROOM_BYTES
            );
        }
        return ESP_OK;
    }

    ESP_LOGE(
        TAG,
        "Display DMA acquisition failed: row=%lu max_rows=%lu largest=%lu "
        "emergency_in_use=%d error=%s",
        (unsigned long)source_row_bytes,
        (unsigned long)maximum_transfer_rows,
        (unsigned long)largest_block,
        display_dma_emergency_in_use,
        esp_err_to_name(ESP_ERR_NO_MEM)
    );
    return ESP_ERR_NO_MEM;
}

#endif
