#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "bsp/display.h"

#define BSP_DISPLAY_DRAW_BUFFER_HEIGHT     64U
#define BSP_DISPLAY_DMA_TRANSFER_HEIGHT    8U
// Synchronous standalone writes use up to 32 rows. Native frames overlap two
// 16-row slots; Luna's UI keeps its existing 8-row two-slot pipeline.
#define BSP_DISPLAY_STANDALONE_DMA_TRANSFER_HEIGHT 32U
// Two 16-row render slots retain the previous 32-row internal DMA budget.
#define BSP_DISPLAY_STANDALONE_FRAME_SLOT_HEIGHT 16U
#define BSP_DISPLAY_DMA_PIPELINE_DEPTH     2U
#define BSP_DISPLAY_DMA_ALIGNMENT          64U
#define BSP_DISPLAY_DMA_HEADROOM_BYTES     4096U
#define BSP_DISPLAY_DMA_EMERGENCY_ROWS     1U
#define BSP_DISPLAY_REPLAY_TRANSFER_HEIGHT BSP_DISPLAY_DMA_TRANSFER_HEIGHT
#define BSP_DISPLAY_CIRCLE_EDGE_PADDING    2
#define BSP_DISPLAY_CONTROL_LOCK_TIMEOUT_MS 1000U
#define BSP_DISPLAY_DIRECT_TRANSFER_TIMEOUT_MS 1000U
#define BSP_DISPLAY_FRAME_BYTES            \
    ((size_t)BSP_LCD_H_RES * BSP_LCD_V_RES * BSP_LCD_BITS_PER_PIXEL / 8U)
#define BSP_DISPLAY_DMA_ROW_BYTES           \
    ((size_t)BSP_LCD_H_RES * BSP_LCD_BITS_PER_PIXEL / 8U)
#define BSP_DISPLAY_DMA_ALIGNED_BYTES(_bytes) \
    (((_bytes) + BSP_DISPLAY_DMA_ALIGNMENT - 1U) & \
     ~(BSP_DISPLAY_DMA_ALIGNMENT - 1U))
#define BSP_DISPLAY_DMA_SLOT_BYTES          \
    BSP_DISPLAY_DMA_ALIGNED_BYTES(          \
        BSP_DISPLAY_DMA_ROW_BYTES * BSP_DISPLAY_DMA_TRANSFER_HEIGHT \
    )
#define BSP_DISPLAY_DMA_PIPELINE_BYTES       \
    (BSP_DISPLAY_DMA_SLOT_BYTES * BSP_DISPLAY_DMA_PIPELINE_DEPTH)
#define BSP_DISPLAY_DMA_EMERGENCY_BYTES      \
    BSP_DISPLAY_DMA_ALIGNED_BYTES(           \
        BSP_DISPLAY_DMA_ROW_BYTES * BSP_DISPLAY_DMA_EMERGENCY_ROWS \
    )

typedef struct {
    uint8_t *data;
    size_t slot_stride_bytes;
    size_t buffer_count;
    size_t transfer_rows;
    bool heap_owned;
    bool emergency;
} bsp_display_dma_lease_t;

// lease: Pipeline whose DMA completion has already been proven by the caller.
void bsp_display_release_dma_pipeline(bsp_display_dma_lease_t *lease);
// source_row_bytes: Packed bytes per panel row.
// maximum_transfer_rows: Maximum rows per bounded transfer.
// preferred_buffer_count: One or two pipeline slots.
// out_lease: Receives the owned pipeline on success.
esp_err_t bsp_display_acquire_dma_pipeline(
    size_t source_row_bytes, size_t maximum_transfer_rows,
    size_t preferred_buffer_count, bsp_display_dma_lease_t *out_lease
);
// count: Event count to report at the first occurrence and powers of two.
bool bsp_display_should_log_count(uint32_t count);

// Verify the module-owned emergency row before display startup.
esp_err_t bsp_display_validate_emergency_buffer(void);
