# Device HTTP API

HTTP endpoint ownership and bounded JSON serialization of canonical device state.
Public endpoint/JSON declarations stay in `include/`.

Handlers use existing snapshot and service boundaries, not a second hardware
acquisition path. Radio lifecycle remains in `../network/`.
