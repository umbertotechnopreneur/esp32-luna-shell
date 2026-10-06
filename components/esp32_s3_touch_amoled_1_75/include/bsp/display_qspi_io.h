#pragma once

#include "esp_lcd_io_spi.h"

// bus: Initialized dedicated CO5300 QSPI bus.
// config: Board clock, chip select and completion callback.
// output: Receives the native queued-only transport after successful admission.
// Exception: Returns allocation/configuration/SPI errors; never acquires an infinite bus lock.
esp_err_t bsp_display_new_qspi_io(spi_host_device_t bus,
    const esp_lcd_panel_io_spi_config_t *config, esp_lcd_panel_io_handle_t *output);
