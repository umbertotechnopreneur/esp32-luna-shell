#pragma once

// Process-lifetime RGB565 background generated with the canonical ordered Bayer
// pattern. The pixels live in PSRAM so the shared background remains an image,
// not a renderer-dependent gradient.

#include <atomic>
#include <cstdint>

#include "lvgl.h"

namespace qsee {

class BackgroundAsset final {
public:
    static constexpr uint16_t kWidth = 466;
    static constexpr uint16_t kHeight = 466;

    static BackgroundAsset &instance();

    // Start the generator once; the GUI thread only observes the published image.
    void request();

    [[nodiscard]] const lv_image_dsc_t *image() const;

private:
    enum class State : uint8_t {
        Idle,
        Working,
        Ready,
        Failed,
    };

    BackgroundAsset() = default;

    static void worker_entry(void *argument);
    void run_worker();
    void publish(uint8_t *pixels);

    std::atomic<State> state_{State::Idle};
    lv_image_dsc_t image_{};
};

}  // namespace qsee
