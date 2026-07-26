#include "qsee_pairing_qr.hpp"

#include <algorithm>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "qrcode.h"

namespace {

constexpr char kTag[] = "qsee_pairing_qr";
constexpr int kQuietZoneModules = 4;

}  // namespace

namespace qsee {

PairingQrAsset *PairingQrAsset::rendering_ = nullptr;

PairingQrAsset::~PairingQrAsset()
{
    reset();
}

void PairingQrAsset::reset()
{
    if (pixels_ != nullptr) {
        heap_caps_free(pixels_);
        pixels_ = nullptr;
    }
    stride_ = 0;
    data_size_ = 0;
    image_ = {};
}

esp_err_t PairingQrAsset::generate(const char *payload)
{
    if (payload == nullptr || payload[0] == '\0') {
        ESP_LOGE(kTag, "QR generation rejected an empty payload");
        return ESP_ERR_INVALID_ARG;
    }
    if (rendering_ != nullptr) {
        ESP_LOGE(kTag, "QR generation rejected a concurrent renderer");
        return ESP_ERR_INVALID_STATE;
    }

    if (pixels_ == nullptr) {
        stride_ = lv_draw_buf_width_to_stride(kSize, LV_COLOR_FORMAT_L8);
        data_size_ = stride_ * kSize;
        pixels_ = static_cast<uint8_t *>(
            heap_caps_aligned_alloc(
                4,
                data_size_,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
            )
        );
        if (pixels_ == nullptr) {
            pixels_ = static_cast<uint8_t *>(
                heap_caps_aligned_alloc(
                    4,
                    data_size_,
                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
                )
            );
        }
        if (pixels_ == nullptr) {
            ESP_LOGE(
                kTag,
                "QR image allocation failed for %lu bytes",
                static_cast<unsigned long>(data_size_)
            );
            return ESP_ERR_NO_MEM;
        }

        image_ = {};
        image_.header.magic = LV_IMAGE_HEADER_MAGIC;
        image_.header.cf = LV_COLOR_FORMAT_L8;
        image_.header.flags = 0;
        image_.header.w = kSize;
        image_.header.h = kSize;
        image_.header.stride = stride_;
        image_.data_size = data_size_;
        image_.data = pixels_;
    }

    std::memset(pixels_, 0xFF, data_size_);
    rendering_ = this;
    esp_qrcode_config_t config = ESP_QRCODE_CONFIG_DEFAULT();
    config.display_func = render_callback;
    config.max_qrcode_version = 10;
    config.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
    const esp_err_t result = esp_qrcode_generate(&config, payload);
    rendering_ = nullptr;
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "QR generation failed: %s", esp_err_to_name(result));
    }
    return result;
}

const lv_image_dsc_t *PairingQrAsset::image() const
{
    return pixels_ != nullptr ? &image_ : nullptr;
}

void PairingQrAsset::render_callback(const uint8_t *qrcode)
{
    if (rendering_ != nullptr) {
        rendering_->render(qrcode);
    }
}

void PairingQrAsset::render(const uint8_t *qrcode)
{
    if (qrcode == nullptr || pixels_ == nullptr) {
        return;
    }

    const int modules = esp_qrcode_get_size(qrcode);
    const int scale = std::max(
        1,
        static_cast<int>(kSize) /
            (modules + (kQuietZoneModules * 2))
    );
    const int rendered =
        (modules + (kQuietZoneModules * 2)) * scale;
    const int offset = (static_cast<int>(kSize) - rendered) / 2;

    for (int y = 0; y < rendered; ++y) {
        for (int x = 0; x < rendered; ++x) {
            const int module_x = (x / scale) - kQuietZoneModules;
            const int module_y = (y / scale) - kQuietZoneModules;
            const bool dark =
                module_x >= 0 &&
                module_y >= 0 &&
                module_x < modules &&
                module_y < modules &&
                esp_qrcode_get_module(qrcode, module_x, module_y);
            pixels_[
                (static_cast<size_t>(offset + y) * stride_) +
                static_cast<size_t>(offset + x)
            ] = dark ? 0x00U : 0xFFU;
        }
    }
}

}  // namespace qsee
