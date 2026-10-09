#pragma once

#include "esp_err.h"

// QR transport is outside this fixture; only its compile-time SDK declarations
// are supplied. Any accidental call remains an unresolved symbol at link time.
using esp_qrcode_handle_t = void *;
constexpr int ESP_QRCODE_ECC_MED = 1;
struct esp_qrcode_config_t {
    int max_qrcode_version = 0;
    int qrcode_ecc_level = 0;
    void (*display_func)(esp_qrcode_handle_t) = nullptr;
};
#define ESP_QRCODE_CONFIG_DEFAULT() esp_qrcode_config_t{}
int esp_qrcode_get_size(esp_qrcode_handle_t handle);
bool esp_qrcode_get_module(esp_qrcode_handle_t handle, int column, int row);
esp_err_t esp_qrcode_generate(const esp_qrcode_config_t *configuration, const char *payload);
