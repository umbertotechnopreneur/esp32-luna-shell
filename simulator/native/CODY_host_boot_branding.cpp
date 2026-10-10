// Compile the unchanged production C decoder/resources as C++ for the existing host ESP error/log transport.
#include "../../components/LUNASHELL_boot/src/ui/VIBEWARE_boot_animation.c"
#include "../../assets/branding/VibeWare/generated/vibeware_boot_data.c"
#include "CODY_boot_branding.hpp"
#include "scene_data.hpp"
#include <cmath>
#include <new>

#if defined(_WIN32)
#define CODY_HOST_EXPORT extern "C" __declspec(dllexport)
#else
#define CODY_HOST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
struct HostBootBranding {
    CODY::BootBranding::Presentation presentation{};
    CODY::BootBranding::Scratch scratch{};
};
}

// output: Receives a heap-owned prepared native branding renderer with bounded row scratch.
// Exception: Returns original resource/font/layout/allocation errors, never fallback artwork.
CODY_HOST_EXPORT esp_err_t cody_host_boot_branding_create(void **output)
{
    if (output == nullptr) {
        return CODY::BootBranding::reject("Host owner output", ESP_ERR_INVALID_ARG);
    }
    esp_err_t error = VIBEWARE_boot_validate();
    if (error != ESP_OK) {
        return error;
    }
    auto *owner = new (std::nothrow) HostBootBranding{};
    if (owner == nullptr) {
        return CODY::BootBranding::reject("Host owner allocation", ESP_ERR_NO_MEM);
    }
    error = CODY::BootBranding::prepare(CODY::SceneData::title_font,
        CODY::SceneData::body_font, owner->presentation);
    if (error != ESP_OK) {
        delete owner;
        return error;
    }
    *output = owner;
    return ESP_OK;
}

// context: Heap-owned host renderer released only after serialized rendering finishes.
CODY_HOST_EXPORT void cody_host_boot_branding_destroy(void *context)
{
    delete static_cast<HostBootBranding *>(context);
}

// elapsed_ms: Original nonnegative artistic preview clock, independent of sensor/runtime telemetry.
// output: Receives the approved resource ordinal using its actual 42 frame durations.
// Exception: Rejects nonfinite/negative time or inconsistent compiled duration metadata.
CODY_HOST_EXPORT esp_err_t cody_host_boot_branding_frame_index(double elapsed_ms, uint16_t *output)
{
    if (output == nullptr || !std::isfinite(elapsed_ms) || elapsed_ms < 0.0) {
        ESP_LOGE("CodyBootBranding", "Host clock rejected: elapsed_ms=%g output=%p", elapsed_ms,
            static_cast<void *>(output));
        return CODY::BootBranding::reject("Host animation clock", ESP_ERR_INVALID_ARG);
    }
    double remaining = std::fmod(elapsed_ms, static_cast<double>(VIBEWARE_BOOT_DURATION_MS));
    for (uint16_t index = 0; index < VIBEWARE_BOOT_FRAME_COUNT; ++index) {
        if (remaining < VIBEWARE_boot_frames[index].duration_ms) {
            *output = index;
            return ESP_OK;
        }
        remaining -= VIBEWARE_boot_frames[index].duration_ms;
    }
    return CODY::BootBranding::reject("Host frame timing", ESP_ERR_INVALID_SIZE);
}

// context: Serialized heap-owned renderer; neither scratch nor pixels enter device snapshots.
// frame_index: Original approved GIF ordinal, retaining the complete animation sequence.
// pixels: Exclusively borrowed host-owned full RGB565 canvas.
// capacity: Actual writable output extent in pixels.
// Exception: Returns every actual font, resource, geometry or raster error.
CODY_HOST_EXPORT esp_err_t cody_host_boot_branding_frame(void *context, uint16_t frame_index,
    uint16_t *pixels, size_t capacity)
{
    auto *owner = static_cast<HostBootBranding *>(context);
    if (owner == nullptr || pixels == nullptr || capacity !=
        static_cast<size_t>(CODY::BootBranding::kCanvas) * CODY::BootBranding::kCanvas) {
        ESP_LOGE("CodyBootBranding", "Host canvas rejected: owner=%p pixels=%p capacity=%zu",
            context, static_cast<void *>(pixels), capacity);
        return CODY::BootBranding::reject("Host canvas extent", ESP_ERR_INVALID_ARG);
    }
    CODY2D::Surface surface{pixels, CODY::BootBranding::kCanvas, CODY::BootBranding::kCanvas,
        CODY::BootBranding::kCanvas, static_cast<uint32_t>(capacity), CODY2D::Rgb565ByteOrder::Native};
    return CODY::BootBranding::draw_rows(owner->presentation, owner->scratch, frame_index, surface, 0);
}
