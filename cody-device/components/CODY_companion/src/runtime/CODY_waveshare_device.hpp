/* Copyright (c) 2026 Umberto Giacobbi */
#pragma once

#include "CODY_device_capabilities.hpp"
#include "bsp/display.h"
#include "sdkconfig.h"

namespace CODY::Waveshare {

// No parameters: compose the enabled native Cody profile from its BSP and build options.
constexpr DeviceCapabilities device_capabilities()
{
    DeviceCapabilities device{};
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISPLAY
    static_assert(BSP_LCD_COLOR_FORMAT == ESP_LCD_COLOR_FORMAT_RGB565);
    // Both supported Waveshare adapters share this authored safe content region.
    device.geometry = {BSP_LCD_H_RES, BSP_LCD_V_RES, DisplayShape::Circle, {73, 73, 320, 320}};
    device.pixel_format = DisplayPixelFormat::Rgb565;
    device.rotation = DisplayRotationSupport::Orthogonal;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_TOUCH
    device.touch = true;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_POWER_INPUTS
    device.boot_button = true;
    device.power_button = true;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_AUDIO && CONFIG_LUNASHELL_BUILD_AUDIO_OUTPUT_DRIVER_ES8311 && CONFIG_CODEC_ES8311_SUPPORT
    device.speaker = true;
#endif
#if CONFIG_LUNASHELL_BUILD_VOICE_CAPTURE_AVAILABLE
    device.microphone_channels = CONFIG_LUNASHELL_BUILD_VOICE_MICROPHONE_CHANNELS;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_VOICE_INPUT
    device.voice_recording = device.microphone_channels != 0;
#endif
    device.persistent_settings = true; // Internal NVS, independent of the SD profile.
#if CONFIG_LUNASHELL_BUILD_ENABLE_REMOVABLE_STORAGE
    device.removable_storage = true;
#endif
    device.internal_ram = true;
#if CONFIG_SPIRAM_BOOT_INIT && (CONFIG_SPIRAM_USE_CAPS_ALLOC || CONFIG_SPIRAM_USE_MALLOC)
    device.external_ram_heap = true;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_BATTERY
    device.battery_measurement = true;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_IMU
    device.motion_sensor = true;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_RTC
    device.external_rtc = true;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_GNSS
    device.gnss = true;
#endif
#if CONFIG_LUNASHELL_BUILD_ENABLE_DISTANCE && CONFIG_LUNASHELL_BUILD_DISTANCE_DRIVER_TOF400F_DUAL_UART
    device.distance = true;
#endif
    return device;
}

inline constexpr auto kDevice = device_capabilities();

} // namespace CODY::Waveshare
