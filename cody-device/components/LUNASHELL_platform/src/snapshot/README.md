<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

# Snapshot ownership

Owns hardware acquisition, canonical snapshot mutation/reports, sampling cadence,
sensor fusion and measured performance. `LUNASHELL_snapshot_internal.hpp` is private
to this component, including the clock and voice acquisition owners.

Consumers use the public snapshot interface in `include/`; successful reads,
original timestamps and real failures remain owned here.

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
