#pragma once

#include <array>
#include <cstdint>
#include "esp_err.h"

namespace CODY {

enum class OverlayMode : uint8_t { Artwork, Skeleton };

struct FrameCosts {
    uint32_t geometry_us = 0;
    uint32_t raster_us = 0;
    uint32_t transfer_us = 0;
    uint32_t total_us = 0;
};

struct FrameDistribution {
    uint32_t count = 0;
    FrameCosts mean{};
    uint32_t median_us = 0;
    uint32_t p95_us = 0;
    uint32_t maximum_us = 0;
    double standard_deviation_us = 0;
    uint32_t missed_deadlines = 0;
};

// A bounded rolling window per overlay mode. No frame-time heap allocation.
class FrameBenchmark {
public:
    static constexpr uint32_t kCapacity = 128;

    // mode: Semantic overlay identity, independent from storage traversal.
    // costs: Measured stages of one successfully completed frame.
    esp_err_t record(OverlayMode mode, const FrameCosts &costs);
    // mode: Overlay identity whose measured window is summarized.
    // budget_us: Explicit target frame time; exceeding it is a missed deadline.
    // output: Receives exact statistics for the retained completed frames.
    esp_err_t summarize(OverlayMode mode, uint32_t budget_us, FrameDistribution &output) const;
    // An explicit route entry/resume starts fresh measurement windows.
    void reset();

private:
    struct Window {
        std::array<FrameCosts, kCapacity> frames{};
        uint32_t count = 0;
        uint32_t next = 0;
    };
    Window artwork_{};
    Window skeleton_{};
};
} // namespace CODY
