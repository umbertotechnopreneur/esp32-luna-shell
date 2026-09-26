#pragma once
#include <cstdint>
namespace LUNASHELL {
enum class DeviceOperatingState { Active, EnteringStandby, Standby, Waking };
struct PowerSnapshot {
    DeviceOperatingState state = DeviceOperatingState::Active;
    uint32_t transition_sequence = 0;
};
struct DeviceSnapshot { PowerSnapshot power; };
class DeviceSnapshotService {
public:
    DeviceSnapshot current;
    static DeviceSnapshotService &instance() { static DeviceSnapshotService service; return service; }
    DeviceSnapshot snapshot() const { return current; }
};
}
