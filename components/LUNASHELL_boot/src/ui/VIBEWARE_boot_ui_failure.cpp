#include "LUNASHELL_device_snapshot.hpp"

// error: Original asynchronous LVGL boot failure; never converted into a healthy display.
extern "C" void VIBEWARE_boot_publish_display_failure(esp_err_t error)
{
    LUNASHELL::DeviceSnapshotService::instance().reportDisplay(false, error);
}
