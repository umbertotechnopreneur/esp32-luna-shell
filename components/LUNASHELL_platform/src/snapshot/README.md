# Snapshot ownership

Owns hardware acquisition, canonical snapshot mutation/reports, sampling cadence,
sensor fusion and measured performance. `LUNASHELL_snapshot_internal.hpp` is private
to this component, including the clock and voice acquisition owners.

Consumers use the public snapshot interface in `include/`; successful reads,
original timestamps and real failures remain owned here.
