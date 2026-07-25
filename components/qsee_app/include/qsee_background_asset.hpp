#pragma once

// Process-lifetime cache for the full-screen dithered background image.
// SD persists the asset; LVGL always renders from the PSRAM copy.

#include <atomic>
#include <cstdint>

#include "lvgl.h"

namespace qsee {

class BackgroundAssetCache final {
public:
    /** Return the single cache whose image descriptor remains valid for the process lifetime. */
    static BackgroundAssetCache &instance();

    /** Start one low-priority load-or-generate worker after storage becomes ready. */
    void request();

    /** Return the published PSRAM-backed image, or null while the gradient fallback is active. */
    [[nodiscard]] const lv_image_dsc_t *image() const;

private:
    enum class State : uint8_t {
        Idle,
        Working,
        Ready,
        Failed,
    };

    BackgroundAssetCache() = default;

    /** Bridge the FreeRTOS task entrypoint to the process-lifetime cache instance. */
    static void workerEntry(void *argument);

    /** Load a validated cache or generate it once without ever touching LVGL. */
    void runWorker();

    /** Publish fully initialized pixels and descriptor to the LVGL thread. */
    void publish(uint8_t *pixels);

    std::atomic<State> _state{State::Idle};
    uint8_t *_pixels = nullptr;
    lv_image_dsc_t _image{};
};

}  // namespace qsee
