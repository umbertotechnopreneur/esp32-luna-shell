#include "sdkconfig.h"

#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY && !CONFIG_LUNASHELL_BUILD_ENABLE_UI
#include "bsp/display_qspi_io.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define QSPI_IO_WAIT_MS 100U
#define QSPI_PARAMETER_BYTES 64U

static const char *TAG = "bsp_display_qspi";

typedef struct {
    esp_lcd_panel_io_t base;
    spi_device_handle_t device;
    spi_transaction_ext_t transaction;
    esp_lcd_panel_io_color_trans_done_cb_t completion;
    void *completion_context;
    bool color_transaction;
    bool pending;
    esp_err_t fault;
    uint8_t parameters[QSPI_PARAMETER_BYTES] __attribute__((aligned(4)));
} bsp_qspi_io_t;

// io: Transport with one immutable descriptor until its result is returned.
// operation: Specific operation whose real failure is retained and logged.
// error: Original SPI or ownership failure.
static esp_err_t retain_fault(bsp_qspi_io_t *io, const char *operation, esp_err_t error)
{
    if (io->fault == ESP_OK) {
        io->fault = error;
    }
    ESP_LOGE(TAG, "%s failed: %s; pending=%d", operation, esp_err_to_name(error), io->pending);
    return error;
}

// io: Sole display-owner transport, retaining pending descriptor and DMA ownership.
// Exception: A result timeout poisons this transport until reset; no descriptor is reused.
static esp_err_t retire_transaction(bsp_qspi_io_t *io)
{
    if (io->fault != ESP_OK) {
        return io->fault;
    }
    if (!io->pending) {
        return ESP_OK;
    }
    spi_transaction_t *completed = NULL;
    const esp_err_t error = spi_device_get_trans_result(io->device, &completed,
        pdMS_TO_TICKS(QSPI_IO_WAIT_MS));
    if (error != ESP_OK) {
        return retain_fault(io, "SPI result retirement", error);
    }
    if (completed != &io->transaction.base) {
        return retain_fault(io, "SPI descriptor identity", ESP_ERR_INVALID_STATE);
    }
    io->pending = false;
    return ESP_OK;
}

// transaction: Internal immutable descriptor whose physical transfer has completed.
static void color_completed(spi_transaction_t *transaction)
{
    bsp_qspi_io_t *io = transaction->user;
    if (io->color_transaction && io->completion != NULL) {
        // The SPI result queue, rather than this early ISR notification, owns
        // descriptor retirement. The frame callback only releases its pixel lease.
        io->completion(&io->base, NULL, io->completion_context);
    }
}

// io: Transport already proven idle by result retirement.
// command: CO5300 8-bit opcode and 24-bit register/address header, or -1 for no header.
// data: Internal DMA-capable parameter scratch or caller-owned DMA pixel lease.
// bytes: Exact one-transaction payload admitted by the SPI bus.
// color: Select four data wires for pixels; command/address always use one wire.
// Exception: A rejected queue retains its real error and never reports completion.
static esp_err_t queue_transaction(bsp_qspi_io_t *io, int command,
    const void *data, size_t bytes, bool color)
{
    memset(&io->transaction, 0, sizeof(io->transaction));
    io->transaction.command_bits = command < 0 ? 0 : 8;
    io->transaction.address_bits = command < 0 ? 0 : 24;
    io->transaction.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
    if (color) {
        io->transaction.base.flags |= SPI_TRANS_MODE_QIO;
    }
    io->transaction.base.cmd = command < 0 ? 0 : ((uint32_t)command >> 24);
    io->transaction.base.addr = command < 0 ? 0 : ((uint32_t)command & UINT32_C(0x00FFFFFF));
    io->transaction.base.tx_buffer = data;
    io->transaction.base.length = bytes * 8U;
    io->transaction.base.user = io;
    io->color_transaction = color;
    // Publication precedes queueing because the IRQ can run before queue returns.
    io->pending = true;
    const esp_err_t error = spi_device_queue_trans(io->device, &io->transaction.base,
        pdMS_TO_TICKS(QSPI_IO_WAIT_MS));
    if (error != ESP_OK) {
        io->pending = false;
        return retain_fault(io, "SPI queue admission", error);
    }
    return ESP_OK;
}

// base: Native CO5300 transport called under the display owner's lock.
// command: Authored QSPI register command.
// parameters: Source bytes copied into internal scratch before DMA admission.
// bytes: Zero through the explicitly bounded parameter capacity.
// Exception: Returns real validation, prior retirement, queue and result errors.
static esp_err_t transmit_parameters(esp_lcd_panel_io_t *base, int command,
    const void *parameters, size_t bytes)
{
    bsp_qspi_io_t *io = (bsp_qspi_io_t *)base;
    if (bytes > QSPI_PARAMETER_BYTES || (bytes != 0 && parameters == NULL)) {
        return retain_fault(io, "Parameter bounds", ESP_ERR_INVALID_ARG);
    }
    esp_err_t error = retire_transaction(io);
    if (error != ESP_OK) {
        return error;
    }
    if (bytes != 0) {
        memcpy(io->parameters, parameters, bytes);
    }
    error = queue_transaction(io, command, io->parameters, bytes, false);
    return error == ESP_OK ? retire_transaction(io) : error;
}

// base: Native CO5300 transport owned by the current display frame.
// command: Authored RAMWR QSPI header.
// pixels: Internal DMA-capable immutable lease retained through completion.
// bytes: Exact nonzero pixel payload, never split into untracked transfers.
// Exception: Returns original validation/retirement/queue errors.
static esp_err_t transmit_pixels(esp_lcd_panel_io_t *base, int command,
    const void *pixels, size_t bytes)
{
    bsp_qspi_io_t *io = (bsp_qspi_io_t *)base;
    if (pixels == NULL || bytes == 0 || !esp_ptr_dma_capable(pixels) ||
        ((uintptr_t)pixels & 3U) != 0 || bytes > UINT32_MAX / 8U) {
        return retain_fault(io, "Pixel lease bounds", ESP_ERR_INVALID_ARG);
    }
    const esp_err_t error = retire_transaction(io);
    return error == ESP_OK ? queue_transaction(io, command, pixels, bytes, true) : error;
}

// base: Write-only CO5300 transport.
// command: Requested unsupported read command.
// parameters: Caller output, never populated with synthetic bytes.
// bytes: Requested read length.
// Exception: Read operations are explicitly unsupported.
static esp_err_t receive_parameters(esp_lcd_panel_io_t *base, int command,
    void *parameters, size_t bytes)
{
    (void)command;
    (void)parameters;
    (void)bytes;
    return retain_fault((bsp_qspi_io_t *)base, "Parameter read", ESP_ERR_NOT_SUPPORTED);
}

// base: Sole-owner transport whose queued descriptor must retire before destruction.
// Exception: Uncertain completion retains the entire transport until reset.
static esp_err_t destroy_transport(esp_lcd_panel_io_t *base)
{
    bsp_qspi_io_t *io = (bsp_qspi_io_t *)base;
    const esp_err_t retirement_error = retire_transaction(io);
    if (retirement_error != ESP_OK) {
        return retirement_error;
    }
    const esp_err_t error = spi_bus_remove_device(io->device);
    if (error != ESP_OK) {
        return retain_fault(io, "SPI device removal", error);
    }
    free(io);
    return ESP_OK;
}

// base: Idle transport; callback lifetime is the panel's lifetime.
// callbacks: Real color-transfer completion callback.
// context: Caller-owned stable completion semaphore/context.
// Exception: Rejects callback mutation while a transaction is pending or faulted.
static esp_err_t register_callbacks(esp_lcd_panel_io_t *base,
    const esp_lcd_panel_io_callbacks_t *callbacks, void *context)
{
    bsp_qspi_io_t *io = (bsp_qspi_io_t *)base;
    if (callbacks == NULL || io->pending || io->fault != ESP_OK) {
        return retain_fault(io, "Completion callback registration", ESP_ERR_INVALID_STATE);
    }
    io->completion = callbacks->on_color_trans_done;
    io->completion_context = context;
    return ESP_OK;
}

// bus: Initialized dedicated CO5300 QSPI bus.
// config: Exact native board clock/chip-select/callback configuration.
// output: Receives transport only after SPI admission succeeds.
// Exception: Returns real configuration, allocation and SPI-device errors.
esp_err_t bsp_display_new_qspi_io(spi_host_device_t bus,
    const esp_lcd_panel_io_spi_config_t *config, esp_lcd_panel_io_handle_t *output)
{
    if (output != NULL) {
        *output = NULL;
    }
    if (config == NULL || output == NULL || !config->flags.quad_mode ||
        config->lcd_cmd_bits != 32 || config->lcd_param_bits != 8 || config->dc_gpio_num != -1) {
        ESP_LOGE(TAG, "Native QSPI configuration rejected: %s", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }
    bsp_qspi_io_t *io = heap_caps_calloc(1, sizeof(*io), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (io == NULL) {
        ESP_LOGE(TAG, "Internal transport allocation failed: %s", esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
    const spi_device_interface_config_t device = {
        .command_bits = 8,
        .address_bits = 24,
        .mode = config->spi_mode,
        .clock_speed_hz = config->pclk_hz,
        .spics_io_num = config->cs_gpio_num,
        .queue_size = 1,
        .cs_ena_pretrans = config->cs_ena_pretrans,
        .cs_ena_posttrans = config->cs_ena_posttrans,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .post_cb = color_completed,
    };
    const esp_err_t error = spi_bus_add_device(bus, &device, &io->device);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "SPI device admission failed: %s", esp_err_to_name(error));
        free(io);
        return error;
    }
    io->base.tx_param = transmit_parameters;
    io->base.tx_color = transmit_pixels;
    io->base.rx_param = receive_parameters;
    io->base.del = destroy_transport;
    io->base.register_event_callbacks = register_callbacks;
    io->completion = config->on_color_trans_done;
    io->completion_context = config->user_ctx;
    *output = &io->base;
    ESP_LOGI(TAG, "Native queued-only QSPI: result timeout=%u ms; command/address=8/24 bits",
        QSPI_IO_WAIT_MS);
    return ESP_OK;
}
#endif
