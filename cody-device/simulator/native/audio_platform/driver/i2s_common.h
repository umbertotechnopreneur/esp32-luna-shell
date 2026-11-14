#pragma once

#include <stdint.h>

// ESP-IDF 6.0.2 default DMA geometry used by the unmodified drain calculation.
typedef struct { uint32_t dma_desc_num; uint32_t dma_frame_num; } i2s_chan_config_t;
#define I2S_ROLE_MASTER 0
#define I2S_CHANNEL_DEFAULT_CONFIG(port, role) {6, 240}
