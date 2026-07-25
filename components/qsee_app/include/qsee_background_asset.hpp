/*
 * QSEE.AI
 * Clear signals. Confident decisions.
 *
 * Copyright (c) 2026 QSee.ai. All rights reserved.
 *
 * Author: Umberto Giacobbi
 * QSee.ai: https://qsee.ai
 * Author website: https://umbertogiacobbi.biz
 * Contact: hello@qsee.ai
 */

#pragma once

#include <atomic>
#include <cstdint>

#include "lvgl.h"

namespace qsee {

// Own the process-lifetime PSRAM image used by the Brookesia background resource.
class BackgroundAssetCache final {
public:
    // Return the single cache whose image descriptor remains valid for the process lifetime.
    static BackgroundAssetCache &instance();

    // Start one low-priority load-or-generate worker after storage initialization.
    void request();

    // Return the published PSRAM-backed image, or null while generation is in progress.
    [[nodiscard]] const lv_image_dsc_t *image() const;

    // Report a terminal worker failure so the application can expose it instead of hiding it.
    [[nodiscard]] bool failed() const;

private:
    enum class State : uint8_t {
        Idle,
        Working,
        Ready,
        Failed,
    };

    BackgroundAssetCache() = default;

    // Bridge the FreeRTOS task entrypoint to the process-lifetime cache instance.
    static void worker_entry(void *argument);

    // Load a validated cache or generate it once without touching the GUI runtime.
    void run_worker();

    // Publish fully initialized pixels and descriptor to the Brookesia GUI thread.
    void publish(uint8_t *pixels);

    std::atomic<State> state_{State::Idle};
    uint8_t *pixels_ = nullptr;
    lv_image_dsc_t image_{};
};

}  // namespace qsee
