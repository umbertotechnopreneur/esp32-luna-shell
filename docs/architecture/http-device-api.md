# Device HTTP API

The firmware exposes a small read-only HTTP API on port `8080` when the Wi-Fi
station has a DHCP lease. The server is deliberately tied to the station
lifecycle: it starts after `IP_EVENT_STA_GOT_IP` and stops when the lease or
station is lost.

## Endpoints

| Method | Path | Authentication | Purpose |
| --- | --- | --- | --- |
| `GET` | `/api/v1/health` | None | Lightweight liveness/readiness probe |
| `GET` | `/api/v1/info` | API key | Complete immutable `DeviceSnapshotService` snapshot |
| `GET` | `/api/v1/rtc` | API key | RTC date/time, integrity, synchronization and source |
| `GET` | `/api/v1/gps` | API key | GPS health, fix, coordinates, satellites and NMEA metadata |
| `GET` | `/api/v1/status` | API key | Compact health/status view for every device component |

The default API key is shared by the devices and is read from the user setting
`ApiKey`:

```json
{ "ApiKey": "ap-0000000000000000001" }
```

The user may replace it in Settings. Requests may send either:

```http
Authorization: Bearer <api-key>
```

or:

```http
X-API-Key: <api-key>
```

The key is never written to logs. After a Settings change, the Settings owner
must call `qsee::httpServerReloadApiKey()` so the running server adopts the new
value. An authorized diagnostic surface can obtain the active value through
`qsee::httpServerGetApiKey()` into caller-owned storage; this keeps key
disclosure separate from the network API itself.

All data routes serialize the same point-in-time copy returned by
`DeviceSnapshotService::snapshot()`. They do not refresh hardware or access
the I2C peripherals from the HTTP task. GPS and RTC values therefore include
the snapshot sequence and refresh timing available from `/api/v1/info`.
