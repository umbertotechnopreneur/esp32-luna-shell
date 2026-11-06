#pragma once

#include <cstdint>
#include "sdkconfig.h"

namespace LUNASHELL::board {

enum class PhysicalBoard : uint8_t {
    WaveshareAmoled175Gps,
    WaveshareAmoled175C,
};

enum class ImuWakeOutput : uint8_t {
    Int1 = 1,
    Int2 = 2,
};

#if CONFIG_LUNASHELL_BUILD_BOARD_PROFILE_WAVESHARE_ESP32S3_TOUCH_AMOLED_1_75C
inline constexpr PhysicalBoard kPhysicalBoard = PhysicalBoard::WaveshareAmoled175C;
inline constexpr char kName[] = "Waveshare ESP32-S3-Touch-AMOLED-1.75C";
#else
inline constexpr PhysicalBoard kPhysicalBoard = PhysicalBoard::WaveshareAmoled175Gps;
inline constexpr char kName[] = "Waveshare ESP32-S3-Touch-AMOLED-1.75 GPS";
#endif
inline constexpr bool kIs175C = kPhysicalBoard == PhysicalBoard::WaveshareAmoled175C;
// Mounting correction for IMU-derived display orientation, independent of user rotation.
inline constexpr uint16_t kImuDisplayOrientationOffsetDegrees = kIs175C ? 180U : 0U;
inline constexpr ImuWakeOutput kImuWakeOutput =
    static_cast<ImuWakeOutput>(CONFIG_LUNASHELL_BOARD_IMU_WAKE_OUTPUT);
static_assert(kImuWakeOutput == ImuWakeOutput::Int1 || kImuWakeOutput == ImuWakeOutput::Int2);

}  // namespace LUNASHELL::board
