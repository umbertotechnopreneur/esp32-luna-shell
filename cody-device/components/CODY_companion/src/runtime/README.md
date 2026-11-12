<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# Character runtime

Owns runtime state, task/frame lifecycle, wake/standby coordination and persisted
Cody settings. Coordinates other responsibilities without acquiring sensors.
Snapshot validity and actual service errors remain visible to consumers.

Public contracts: `CODY_companion.h`, `CODY_lifecycle.hpp`, `CODY_settings.hpp`.
Behavior: [runtime guide](../../../../../docs/cody/runtime.md).

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
