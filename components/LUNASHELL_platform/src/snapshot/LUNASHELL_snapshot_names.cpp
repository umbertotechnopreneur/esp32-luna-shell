#include "LUNASHELL_device_snapshot.hpp"

namespace LUNASHELL {
// health: Explicit canonical health identity; unknown remains unknown.
const char *deviceHealthName(DeviceHealth health)
{
    switch (health) {
    case DeviceHealth::Ready:
        return "ready";
    case DeviceHealth::Degraded:
        return "degraded";
    case DeviceHealth::Missing:
        return "missing";
    case DeviceHealth::Unknown:
    default:
        return "unknown";
    }
}

}
