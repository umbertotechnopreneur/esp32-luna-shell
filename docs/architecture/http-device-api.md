# Device HTTP API

The firmware exposes a small read-only HTTP API on port `8080` when the Wi-Fi
station has a DHCP lease. The server is deliberately tied to the station
lifecycle: it starts after `IP_EVENT_STA_GOT_IP` and stops when the lease or
station is lost.

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

The user may replace it in Settings. The running server observes changes to
`ApiKey` and adopts a valid new value immediately. The setting is required;
the server does not generate or substitute another key. Requests must send:

```http
Authorization: Bearer <api-key>
```

The key is never written to logs.

All data routes serialize one coherent point-in-time copy returned by
`DeviceSnapshotService::snapshot()`. A bounded task refreshes the hardware
snapshot every five seconds while the HTTP server is online; request handlers
never access peripheral drivers directly. JSON data responses use a versioned
envelope with `sequence`, `refreshed_at_us` and `age_ms` so clients can detect
stale data.

`/api/v1/info` deliberately omits Wi-Fi SSID/IP/MAC and the raw GPS sentence.
Those fields are available only from the authenticated `/api/v1/diagnostics`
surface. Authentication failures are rate-limited to twenty failures per
sixty-second window and return `429 Too Many Requests` with `Retry-After`.

The service is intended for a trusted local network. It is plain HTTP by
design for this first device integration; TLS should be added before exposing
it beyond that boundary.
