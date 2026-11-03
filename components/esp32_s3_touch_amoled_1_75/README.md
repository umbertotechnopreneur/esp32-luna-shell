# Waveshare 1.75 GPS and 1.75C board support

[![Component Registry](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75/badge.svg)](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75)

This repository adapts the Waveshare BSP for two ESP32-S3 boards with a
466 × 466 touch display. Select the 16 MB GPS model or the 32 MB 1.75C with
the firmware console's `-Board` option. The shared driver uses the selected
board's pin assignments, panel reset and audio clock. See the
[board comparison](../../docs/bsp/boards/purchased-amoled-comparison.md) and
[firmware console](../../scripts/README.md).
