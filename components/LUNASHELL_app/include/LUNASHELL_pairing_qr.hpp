#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "lvgl.h"

namespace LUNASHELL {

// Owns one reusable grayscale image for both Espressif app and device QR payloads.
class PairingQrAsset final {
public:
    static constexpr uint16_t kSize = 140;

    PairingQrAsset() = default;
    ~PairingQrAsset();

    PairingQrAsset(const PairingQrAsset &) = delete;
    PairingQrAsset &operator=(const PairingQrAsset &) = delete;

    esp_err_t generate(const char *payload);
    void reset();
    [[nodiscard]] const lv_image_dsc_t *image() const;

private:
    static void render_callback(const uint8_t *qrcode);
    void render(const uint8_t *qrcode);

    static PairingQrAsset *rendering_;
    uint8_t *pixels_ = nullptr;
    size_t stride_ = 0;
    size_t data_size_ = 0;
    lv_image_dsc_t image_{};
};

}  // namespace LUNASHELL
