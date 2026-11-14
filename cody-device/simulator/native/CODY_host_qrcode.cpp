// Host adapter for the same qrcodegen implementation linked on the device.
// QR payloads remain local to the simulation and are never logged.
#include <cstdint>
#include "qrcode.h"
#include "qrcodegen.h"

#include <new>
#include <vector>

// config: Original Espressif QR settings and synchronous display callback.
// text: Bounded, NUL-terminated payload owned by the native onboarding caller.
// Exception: Preserves allocation and encoder failure; callback data expires on return.
extern "C" esp_err_t esp_qrcode_generate(esp_qrcode_config_t *config, const char *text)
{
    if (!config || !text || !config->display_func || config->max_qrcode_version < 1 ||
        config->max_qrcode_version > qrcodegen_VERSION_MAX) return ESP_ERR_INVALID_ARG;
    enum qrcodegen_Ecc ecc = qrcodegen_Ecc_LOW;
    switch (config->qrcode_ecc_level) {
    case ESP_QRCODE_ECC_MED: ecc = qrcodegen_Ecc_MEDIUM; break;
    case ESP_QRCODE_ECC_QUART: ecc = qrcodegen_Ecc_QUARTILE; break;
    case ESP_QRCODE_ECC_HIGH: ecc = qrcodegen_Ecc_HIGH; break;
    default: break;
    }
    try {
        const auto size = qrcodegen_BUFFER_LEN_FOR_VERSION(config->max_qrcode_version);
        std::vector<uint8_t> qr(size), scratch(size);
        if (!qrcodegen_encodeText(text, scratch.data(), qr.data(), ecc,
                qrcodegen_VERSION_MIN, config->max_qrcode_version, qrcodegen_Mask_AUTO, true)) return ESP_FAIL;
        config->display_func(qr.data());
        return ESP_OK;
    } catch (const std::bad_alloc &) {
        return ESP_ERR_NO_MEM;
    }
}

// qr: Borrowed encoder output, valid only during the original callback.
// Exception: The encoder's own size interpretation is returned unchanged.
extern "C" int esp_qrcode_get_size(esp_qrcode_handle_t qr) { return qrcodegen_getSize(qr); }

// qr: Borrowed encoder output.
// x: Horizontal QR module coordinate.
// y: Vertical QR module coordinate.
// Exception: Out-of-range coordinates follow qrcodegen's original false result.
extern "C" bool esp_qrcode_get_module(esp_qrcode_handle_t qr, int x, int y)
{
    return qrcodegen_getModule(qr, x, y);
}

// qr: Borrowed encoded QR; console display is intentionally not a host capability.
extern "C" void esp_qrcode_print_console(esp_qrcode_handle_t qr) { (void)qr; }
