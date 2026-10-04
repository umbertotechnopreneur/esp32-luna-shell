# LUNASHELL_tof400f

Optional dual-UART TOF400F driver and host-pure Modbus protocol implementation.
Sources live in [src](src/README.md), public contracts in `include/`, and existing
protocol fixtures in `tests/`.

The component remains empty unless its existing distance/driver gates are enabled.
Platform distance acquisition owns publication to the canonical device snapshot.
