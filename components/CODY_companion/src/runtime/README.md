# Character runtime

Owns runtime state, task/frame lifecycle, wake/standby coordination and persisted
Cody settings. Coordinates other responsibilities without acquiring sensors.
Snapshot validity and actual service errors remain visible to consumers.

Public contracts: `CODY_companion.h`, `CODY_lifecycle.hpp`, `CODY_settings.hpp`.
Behavior: [runtime guide](../../../../docs/architecture/cody/runtime.md).
