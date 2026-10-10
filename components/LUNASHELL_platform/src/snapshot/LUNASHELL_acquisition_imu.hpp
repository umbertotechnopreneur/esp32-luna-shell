#pragma once

#include "LUNASHELL_device_snapshot.hpp"
#include "driver/i2c_master.h"

namespace LUNASHELL::snapshot_detail::imu {
// Decoded physical sample; consumers receive canonical published projections.
struct RawSample {
    float accelX;
    float accelY;
    float accelZ;
    float gyroX;
    float gyroY;
    float gyroZ;
    float temperature;
};
// bus: Existing BSP-owned bus borrowed by the component.
// Exception: Returns the original hardware initialization or publication error.
esp_err_t initialize(i2c_master_bus_handle_t bus);
// No parameters: observe acquisition availability without reading hardware.
bool ready();
// phase: Original acquisition context used for publication and logging.
// verbose: Whether every measured sample is logged.
// track_orientation: Whether the same measured raw sample updates root orientation state.
// Exception: Returns the original acquisition, publication or sensor-fusion error.
esp_err_t sample(const char *phase, bool verbose = true, bool track_orientation = false);
// percent: Validated smoothing value admitted by the persistence owner.
// valid: Whether persistence validated the admitted value.
void set_smoothing(uint32_t percent, bool valid);
// No parameters: return the admitted smoothing value.
uint32_t smoothing_percent();
// No parameters: return whether the persisted setting was admitted.
bool smoothing_valid();
// No parameters: reset sample filtering under the acquisition owner's barrier.
void reset_filter();
// raw: Same acquisition already held by the orientation owner.
float corrected_gyro_z(const RawSample &raw);
}  // namespace LUNASHELL::snapshot_detail::imu

namespace LUNASHELL::snapshot_detail {
// data: Same normalized physical acquisition; never a second hardware read.
// sampled_at_us: Original sample attempt timestamp.
void process_orientation_sample(const imu::RawSample &data, int64_t sampled_at_us);
// orientation: Bounded destination populated by the root orientation owner.
void copy_orientation_snapshot(OrientationSnapshot &orientation);
// snapshot: Existing canonical/staged publication destination, borrowed synchronously.
// error: Original IMU operation failure.
// attempted_at_us: Original acquisition timestamp; no fallback measurement is supplied.
void invalidate_orientation_sample(DeviceSnapshot &snapshot, esp_err_t error,
                                   int64_t attempted_at_us);
}  // namespace LUNASHELL::snapshot_detail
