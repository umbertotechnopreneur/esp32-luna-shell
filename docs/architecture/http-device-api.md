# Device HTTP API

The firmware contains a small read-only HTTP API module on port `8080`. It is
not started during boot: Wi-Fi/BLE startup remains an explicit Settings action
in the migrated UI, and the HTTP module must be initialized only after its
required persisted settings are present. When initialized, the server is tied
to the station lifecycle: it starts after `IP_EVENT_STA_GOT_IP` and stops when
the lease or station is lost.

## Endpoints

| Method | Path | Authentication | Purpose |
| --- | --- | --- | --- |
| `GET` | `/api/v1/health` | None | Lightweight liveness/readiness probe |
| `GET` | `/api/v1/capabilities` | API key | API version, read-only features and endpoint catalogue |
| `GET` | `/api/v1/info` | API key | Snapshot with operational fields and sensitive network fields redacted |
| `GET` | `/api/v1/diagnostics` | API key | Full snapshot, including Wi-Fi identity and raw GPS sentence metadata |
| `GET` | `/api/v1/rtc` | API key | RTC date/time, integrity, synchronization and source |
| `GET` | `/api/v1/gps` | API key | GPS health, fix, coordinates, satellites and NMEA metadata |
| `GET` | `/api/v1/status` | API key | Compact health/status view for every device component |

The API key is shared by the devices and is read directly from the user setting
`ApiKey`:

```json
{ "ApiKey": "ap-0000000000000000001" }
```

The setting is required; the server does not generate or substitute another
key. The persisted `http_enabled` setting is required as well: the server
fails initialization when either setting is absent or invalid. Requests must
send:

```http
Authorization: Bearer <api-key>
```

The non-UI backend exposes `httpServerSetApiKey()` for an explicit settings
action to validate and persist a new key. It writes the setting before updating
the in-memory credential and never starts or stops the server as a side effect.

The key is never written to logs.

All data routes serialize one coherent point-in-time copy returned by
`DeviceSnapshotService::snapshot()`. The HTTP service never owns an acquisition
task: it consumes the same Full Snapshot generations as the UI and reports the
active user cadence through `/api/v1/capabilities`. Request handlers never
access peripheral drivers directly. JSON data responses use a versioned
envelope with `sequence`, `last_update_attempt_at_us`,
`last_update_complete` and `age_ms`. The timestamp is explicitly the last
refresh attempt, so an incomplete refresh cannot be presented as a successful
sample.

IMU and microphone diagnostics are separately enabled streams. A periodic Full
generation may consume one newly committed Diagnostic sample when it is still
younger than both configured cadences. That one-shot reuse never advances the
component `last_attempt_at_us` or `last_success_at_us`: HTTP consumers always
see the timestamp of the real sensor acquisition. Failed, stale,
already-consumed, time-regressed, or source-mismatched data forces a physical
read and retains the real failure state.

## Battery telemetry validity

The `battery` object in `/api/v1/info` and `/api/v1/diagnostics` separates PMIC
sample validity from the availability of a physical cell. A valid PMIC sample
with no detected battery keeps USB, system-rail, charger, and temperature state
available. The object exposes `data_valid`, `data_stale`,
`last_attempt_at_us`, and `last_success_at_us` alongside health/error state,
while cell-only fields use explicit guards:

- `level_percent_valid` controls `level_percent`;
- `battery_mv_valid` controls `battery_mv`.

The serialized field guards are effective guards: they are false whenever the
PMIC sample is failed or stale, even if the underlying last successful cell
sample had been valid. When a guard is false its value is JSON `null`, never a
numeric zero presented as a measurement. Consumers must require current/ready
PMIC health and then check the field guard; `battery_present=false` is a valid
observation rather than a failed PMIC sample.

## Memory telemetry

The `memory` object in `/api/v1/info` and `/api/v1/diagnostics` exposes raw
allocator observations. Existing aggregate capacity fields remain alongside
these internal-heap fields:

- `internal_heap_total_bytes`
- `internal_minimum_free_bytes`
- `internal_allocated_blocks`
- `internal_free_blocks`
- `internal_total_blocks`

The PSRAM allocator is reported independently:

- `psram_minimum_free_bytes`
- `psram_allocated_blocks`
- `psram_free_blocks`
- `psram_total_blocks`

Boot-scoped allocation-failure telemetry uses:

- `allocation_failure_monitor_active`
- `allocation_failure_count`
- `largest_failed_allocation_bytes`
- `failed_allocation_caps_seen_mask`

The failure callback is active before logger, display, and boot UI allocation.
It records only lock-free bounded aggregates; it does not allocate, log, lock,
format text, or query time. A zero count means no failed allocation was
observed only when `allocation_failure_monitor_active` is true.

Free-block counts and largest-free-block sizes are raw values from
`heap_caps_get_info()`. Clients may derive and label a largest-block/free-space
ratio, but the API does not publish an invented fragmentation percentage.
Allocation failures remain visible for the current boot and do not invalidate
a later successful heap measurement.

`/api/v1/info` deliberately omits Wi-Fi SSID/IP/MAC and the raw GPS sentence.
Those fields are available only from the authenticated `/api/v1/diagnostics`
surface. Authentication failures are rate-limited to twenty failures per
sixty-second window and return `429 Too Many Requests` with `Retry-After`.

The service is intended for a trusted local network. It is plain HTTP by
design for this first device integration; TLS should be added before exposing
it beyond that boundary.
