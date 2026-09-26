#pragma once

#include <cstdint>

#include "lvgl.h"

namespace LUNASHELL {

class MascotAsset final {
public:
    static constexpr uint16_t kWidth = 170;
    static constexpr uint16_t kHeight = 198;

    [[nodiscard]] static const lv_image_dsc_t *idle_head_image();
};

}  // namespace LUNASHELL
