// Real production ribbon/gesture contracts; host artwork is explicitly synthetic.
#include "CODY_voice_waveform.hpp"
#include "CODY_voice_recording.hpp"
#include "CODY2D_text.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace CODY::SceneData { extern const CODY2D::MonoFont body_font; }
namespace {
constexpr int kSize = 466;
using Pixels = std::vector<uint16_t>;
// condition: Required production outcome, never a Python reconstruction.
// message: Actionable failing boundary.
void require(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
// x: Logical display column.
// y: Logical display row.
bool inside_circle(int x, int y)
{
    return (x - 233) * (x - 233) + (y - 233) * (y - 233) <= 233 * 233;
}
// time: Synthetic artwork time; not measured microphone PCM.
// energy: Synthetic gain in the real production range.
// rows: Tile height to exercise actual strip clipping.
// order: Storage byte order, normalized on return for comparison.
// background: Native background; nonblack exercises blending, black matches the recording view.
Pixels render(int64_t time, uint8_t energy, unsigned rows, CODY2D::Rgb565ByteOrder order,
              uint16_t background = 0x0821)
{
    CODY::VoiceRibbonFrame frame;
    CODY::prepare_voice_ribbon(time, energy, frame);
    Pixels pixels(kSize * kSize, background);
    for (unsigned row = 0; row < kSize; row += rows) {
        const unsigned height = std::min(rows, unsigned(kSize) - row);
        constexpr uint16_t guard = 0xA55A;
        Pixels tile(kSize * height + 2, guard);
        std::fill(tile.begin() + 1, tile.end() - 1, CODY2D::rgb565_native_to_stored(background, order));
        CODY2D::Surface surface{tile.data() + 1, kSize, static_cast<uint16_t>(height), kSize,
            static_cast<uint32_t>(kSize * height), order};
        require(CODY::draw_voice_ribbon_rows(surface, static_cast<uint16_t>(row), frame) == ESP_OK,
            "native ribbon draw failed");
        require(tile.front() == guard && tile.back() == guard, "ribbon overwrote strip guard");
        for (unsigned index = 0; index < kSize * height; ++index) {
            pixels[row * kSize + index] = CODY2D::rgb565_stored_to_native(tile[index + 1], order);
        }
    }
    return pixels;
}
// No parameters: compare real native raster output across tiling, byte order and artwork phase.
void raster_contract()
{
    require(CODY::kVoiceRibbonTop == 179 && CODY::kVoiceRibbonHeight == 58 &&
        CODY::kVoiceRibbonTop + CODY::kVoiceRibbonHeight / 2 == 208,
        "recording artwork must grow five pixels above and below its retained center");
    require(CODY::kVoiceRibbonDamageTop == 178 && CODY::kVoiceRibbonDamageBottom == 238 &&
        CODY::kVoiceRibbonDamageTop % 2 == 0 && CODY::kVoiceRibbonDamageBottom % 2 == 0,
        "partial recording refresh must retain even DMA boundaries around the odd artwork edges");
    for (const int64_t time : {0LL, 33333LL, 1512345LL, 11999999LL, 12000000LL}) {
        for (const uint8_t energy : {uint8_t(0), uint8_t(100), uint8_t(255)}) {
            const auto full = render(time, energy, kSize, CODY2D::Rgb565ByteOrder::Native);
            for (const unsigned rows : {2U, 16U}) {
                require(full == render(time, energy, rows, CODY2D::Rgb565ByteOrder::Native), "full/strip ribbon mismatch");
                require(full == render(time, energy, rows, CODY2D::Rgb565ByteOrder::ByteSwapped), "swapped/strip ribbon mismatch");
            }
            unsigned changed = 0;
            for (int y = 0; y < kSize; ++y) for (int x = 0; x < kSize; ++x) {
                if (full[y * kSize + x] == 0x0821) continue;
                ++changed;
                require(x >= CODY::kVoiceRibbonLeft && x < CODY::kVoiceRibbonLeft + CODY::kVoiceRibbonWidth &&
                    y >= CODY::kVoiceRibbonTop && y < CODY::kVoiceRibbonTop + CODY::kVoiceRibbonHeight,
                    "ribbon escaped its owned damage bounds");
                require(inside_circle(x, y), "ribbon escaped circular display");
            }
            require(changed > 100, "ribbon disappeared at a valid phase/gain");
        }
    }
    CODY::VoiceRibbonFrame low, high, before, after;
    CODY::prepare_voice_ribbon(1234567, 0, low);
    CODY::prepare_voice_ribbon(1234567, 255, high);
    CODY::prepare_voice_ribbon(11999999, 255, before);
    CODY::prepare_voice_ribbon(12000000, 255, after);
    unsigned low_area = 0, high_area = 0;
    for (unsigned layer = 0; layer < CODY::kVoiceRibbonLayers; ++layer) {
        require(high.nodes[layer][0].radius_q8 == 0 && high.nodes[layer][48].radius_q8 == 0,
            "ribbon endpoints must taper closed");
        for (unsigned node = 0; node < CODY::kVoiceRibbonNodes; ++node) {
            low_area += low.nodes[layer][node].radius_q8;
            high_area += high.nodes[layer][node].radius_q8;
            require(high.nodes[layer][node].radius_q8 >= low.nodes[layer][node].radius_q8,
                "increasing visual gain shrank an authored lobe");
            require(std::abs(int(before.nodes[layer][node].center_q8) - after.nodes[layer][node].center_q8) <= 16 &&
                std::abs(int(before.nodes[layer][node].radius_q8) - after.nodes[layer][node].radius_q8) <= 16,
                "artwork jumped at periodic clock wrap");
            require(std::abs(int(high.nodes[layer][node].center_q8)) + high.nodes[layer][node].radius_q8 + 256 <=
                CODY::kVoiceRibbonHeight / 2 * 256,
                "enlarged artwork and antialiased edge must fit without clipping its prepared lobes");
        }
    }
    require(high_area > low_area, "synthetic energy did not modulate actual ribbon gain");
    require(render(0, 100, 16, CODY2D::Rgb565ByteOrder::Native) !=
        render(500000, 100, 16, CODY2D::Rgb565ByteOrder::Native), "artwork phase is static");
    const auto transitioned = render(500000, 255, kSize, CODY2D::Rgb565ByteOrder::Native);
    CODY::VoiceRibbonFrame next_frame{};
    CODY::prepare_voice_ribbon(500000, 255, next_frame);
    for (const unsigned rows : {2U, 16U}) {
        for (const auto order : {CODY2D::Rgb565ByteOrder::Native, CODY2D::Rgb565ByteOrder::ByteSwapped}) {
            auto partial = render(0, 255, kSize, CODY2D::Rgb565ByteOrder::Native);
            for (unsigned row = CODY::kVoiceRibbonDamageTop; row < CODY::kVoiceRibbonDamageBottom; row += rows) {
                const auto height = std::min(rows, unsigned(CODY::kVoiceRibbonDamageBottom) - row);
                require(row % 2 == 0 && height % 2 == 0, "recording refresh submitted an odd DMA boundary");
                Pixels tile(kSize * height, CODY2D::rgb565_native_to_stored(0x0821, order));
                const CODY2D::Surface surface{tile.data(), kSize, static_cast<uint16_t>(height), kSize,
                    static_cast<uint32_t>(tile.size()), order};
                require(CODY::draw_voice_ribbon_rows(surface, static_cast<uint16_t>(row), next_frame) == ESP_OK,
                    "enlarged recording band could not retire its actual native strips");
                for (unsigned index = 0; index < tile.size(); ++index) {
                    partial[row * kSize + index] = CODY2D::rgb565_stored_to_native(tile[index], order);
                }
            }
            require(partial == transitioned, "partial ribbon retirement left old lobes or skipped an enlarged edge");
        }
    }
    uint16_t pixel = 0;
    require(CODY::draw_voice_ribbon_rows({&pixel, 1, 1, 1, 1}, 0, low) == ESP_ERR_INVALID_ARG,
        "invalid ribbon canvas accepted");
}
// state: Real display-owned recording recognizer.
// start: Original synthetic contact timestamp.
// session: Original synthetic recording identity.
// rotation: Shared logical orientation.
CODY::VoiceRecordingAction tap(CODY::VoiceRecordingGesture &state, int64_t start,
                              int64_t session = 1000000, uint16_t rotation = 0)
{
    require(CODY::observe_voice_recording_touch(state, 233, 233, true, true, start, session, rotation, true) ==
        CODY::VoiceRecordingAction::None, "press dispatched before release");
    return CODY::observe_voice_recording_touch(state, 0, 0, false, false, start + 40000, session, rotation, true);
}
// No parameters: real shared arbitration covers time, session, orientation and cancellation boundaries.
void gesture_contract()
{
    using Action = CODY::VoiceRecordingAction;
    CODY::VoiceRecordingGesture state;
    require(tap(state, 1100000) == Action::None, "first tap sent without double-tap arbitration");
    const auto due = state.send_due_us;
    require(due == 1140000 + CODY::kVoiceDoubleTapUs, "first tap deadline is not 350 ms");
    require(CODY::take_voice_recording_action(state, due, 1000000, 0, true) == Action::None,
        "exact deadline must still admit the second tap");
    require(CODY::take_voice_recording_action(state, due + 1, 1000000, 0, true) == Action::Send,
        "single tap did not send after arbitration");
    require(CODY::take_voice_recording_action(state, due + 2, 1000000, 0, true) == Action::None, "send replayed");
    CODY::reset_voice_recording_gesture(state);
    tap(state, 1100000);
    require(tap(state, state.send_due_us) == Action::Discard, "second tap at350ms failed to discard");
    require(CODY::take_voice_recording_action(state, 2000000, 1000000, 0, true) == Action::None, "discard leaked a send");
    for (unsigned boundary = 0; boundary < 4; ++boundary) {
        CODY::reset_voice_recording_gesture(state);
        tap(state, 1100000);
        if (boundary == 3) CODY::reset_voice_recording_gesture(state); // Acquired error/shake cancels pending action.
        require(CODY::take_voice_recording_action(state, 2000000, boundary == 0 ? 1500000 : 1000000,
            boundary == 1 ? 90 : 0, boundary != 2) == Action::None,
            "session/rotation/ineligible/error boundary leaked the old send");
    }
    for (uint16_t rotation : {uint16_t(0), uint16_t(90), uint16_t(180), uint16_t(270)}) {
        CODY::reset_voice_recording_gesture(state);
        tap(state, 1100000, 1000000, rotation);
        require(CODY::take_voice_recording_action(state, 2000000, 1000000, rotation, true) == Action::Send,
            "shared orientation changed a valid logical tap");
    }
    CODY::reset_voice_recording_gesture(state);
    tap(state, 1100000);
    CODY::observe_voice_recording_touch(state, 233, 233, true, true, 1300000, 1000000, 0, true);
    require(CODY::take_voice_recording_action(state, 1500000, 1000000, 0, true) == Action::None,
        "second held contact failed to postpone send");
    require(CODY::observe_voice_recording_touch(state, 0, 0, false, false, 1540000, 1000000, 0, true) == Action::Discard,
        "valid second contact released after deadline failed to discard");
}
// pixels: Native RGB565 artwork emitted by the production renderer.
// caption: Actual ASCII glyph content, including explicit host-origin label.
// y: Root production scene's logical baseline placement (top edge).
void caption(Pixels &pixels, const char *caption, int y)
{
    const auto &font = CODY::SceneData::body_font;
    const auto length = static_cast<uint16_t>(std::strlen(caption));
    const int width = (length - 1) * font.advance + font.width;
    const int x = 233 - width / 2;
    require(inside_circle(x, y) && inside_circle(x + width - 1, y + font.height - 1), "caption violates circular chord");
    CODY2D::TextLine line;
    require(CODY2D::prepare_text_line(font, {caption, length}, {int16_t(x), int16_t(y)}, 0xFFFF, line) == ESP_OK,
        "actual body font rejected recording caption");
    require(CODY2D::draw_text_rows(line, length, {pixels.data(), kSize, kSize, kSize, uint32_t(pixels.size())}, 0, 0) == ESP_OK,
        "actual body glyph raster failed");
}
// directory: Artifact directory supplied by the public check runner.
void previews(const std::string &directory)
{
    for (unsigned index = 0; index < 90; ++index) {
        auto pixels = render(index * CODY::kVoiceRibbonFrameUs, uint8_t(40 + std::min(index, 90U - index) * 4),
            16, CODY2D::Rgb565ByteOrder::Native, 0);
        caption(pixels, "Listening", 92);
        caption(pixels, "SIMULATED - 16 kHz", 134);
        caption(pixels, CODY::kVoiceSendHint, 260);
        caption(pixels, CODY::kVoiceSilenceHint, 292);
        caption(pixels, CODY::kVoiceDiscardHint, 334);
        caption(pixels, CODY::kVoiceDiscardDetail, 362);
        caption(pixels, "03s elapsed / 57s left", 402);
        char name[32];
        std::snprintf(name, sizeof(name), "/voice-%02u.ppm", index);
        std::ofstream stream(directory + name, std::ios::binary);
        stream << "P6\n466 466\n255\n";
        for (uint16_t pixel : pixels) {
            const unsigned r = (pixel >> 11) & 31, g = (pixel >> 5) & 63, b = pixel & 31;
            const char rgb[] = {char((r << 3) | (r >> 2)), char((g << 2) | (g >> 4)), char((b << 3) | (b >> 2))};
            stream.write(rgb, 3);
        }
        require(stream.good(), "native preview PPM write failed");
    }
}
}
// argc: One artifact directory is required.
// argv: Literal public runner arguments; fixture never touches hardware.
int main(int argc, char **argv)
{
    require(argc == 2, "expected preview artifact directory");
    raster_contract();
    gesture_contract();
    previews(argv[1]);
    std::puts("PASS production ribbon raster, recording gesture arbitration and synthetic native previews");
}
