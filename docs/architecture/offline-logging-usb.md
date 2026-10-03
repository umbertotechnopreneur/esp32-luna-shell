# Offline logging and native USB readback

The canonical logger captures diagnostics before best-effort native USB output.
The removable-storage owner alone mounts and protects the SD lifetime; the
logger never formats or mounts a card. Cody's profile admits this existing
owner with FAT long filenames. The common firmware defaults select native USB
Serial/JTAG as the console so every supported profile has the explicit VFS
dependency required by the logger's USB driver owner.

## Recording and retention

Application startup installs the canonical hook, starts its native USB receiver,
and records a `BOOT START` marker with the reset reason and firmware version,
including in Release. Normal log output uses `usb_serial_jtag_write_bytes` with
zero wait. An absent or non-consuming terminal can lose USB output; its measured
drop counter is exposed by `LUNASHELL_logger_get_sd_status` separately from real
filesystem errors. The physical boot/display problem reported by the user has
not been verified as resolved.

The SD profile retains the first 128 captured records in 32 KiB of internal RAM.
Internal memory permits capture during the existing NVS/cache-off boundaries.
Profiles with SD disabled do not reserve this full backlog. Headless startup
starts the asynchronous storage owner after initial hardware acquisition and
before display/Cody startup. Once SD is mounted and mirroring is enabled, the
writer replays early records in watched batches, flushes them, then consumes
the ordinary queue. It does not wait for USB acknowledgement.

The first mirror activation in a boot moves a nonempty existing
`/sdcard/LUNASHELL/logs/LUNASHELL.log` to `LUNASHELL.log.1`. The current file also
rotates to that same previous file at 2 MiB. This is bounded retention of two
logger-owned files; the previous file can be the preceding runtime segment.
Mirroring obeys the existing persisted `sd_mirror` preference and independent
standby/performance suspension reasons. Cody timing diagnostics observe the
published SD availability; they never overwrite that preference at boot.

Logging begins at application logger initialization. ROM/bootloader output
cannot be captured by this hook. Early-buffer overflow is counted and persisted
as `BOOT LOG INCOMPLETE`; record truncation retains `[TRUNCATED]`. The existing
255-byte record bound and the final unflushed data before unexpected power loss
remain explicit limits. No missing/corrupt preference or filesystem is repaired
implicitly, and no removable card is formatted by logging or readback.

## Read ownership and integrity

The native USB receiver accepts only fixed current/previous identities. It
calls an immutable callback registered by the storage owner. Each read holds
the storage mutex, rejects an unmounted/terminal/standby state, then takes the
logger I/O mutex shared by writing, closing, rotation and readback. Only the
bounded filesystem operation owns the logger watchdog. Both locks and the
watch are released before sending a response over USB.

Read binding is established on every mount even when mirroring is disabled;
it does not create files. An opaque, nonzero 32-bit revision is seeded per boot
and advances on mount/mirror restart/rotation. A host pins that identity and
the first response's persisted file length. A changed identity or a shorter
file fails the transfer. Ordinary appends keep identity and are excluded from
the pinned download. The revision is a diagnostic identity, not authentication.

## ASCII protocol

Requests terminate with LF and contain one eight-digit hexadecimal request
token, a typed filename, and unsigned decimal offset/revision:

```text
LUNA LOG READ 12ab34cd current 0 0
LUNA LOG READ 98ef76ab previous 128 76543
```

Revision zero is allowed only as the initial unpinned request. The receiver
rejects unknown commands, arbitrary paths, malformed integers, non-ASCII input
and overlong requests. A response starts on a new serial line and contains up
to 128 bytes encoded as hexadecimal (`-` for an empty block):

```text
LUNA LOG DATA <token> <revision> <offset> <total-size> <crc32-hex> <hex-data-or-dash>
LUNA LOG ERROR <token> <esp-err-decimal> <original-errno-decimal>
```

CRC32 is the reflected IEEE/ZIP polynomial, with the standard initial/final
complement; firmware uses `esp_rom_crc32_le(0, data, length)`. Errors retain the
actual `esp_err_t` and errno. A missing previous file is a real not-found error.
SD polling independently retains and publishes media/transport/writer failures
in the canonical device snapshot. The receiver never mounts, wakes, formats,
resets, or reads sensors to satisfy a request.

## Host command

`scripts/device.ps1 sd-log` is a host-only route and does not load ESP-IDF. It
uses normal COM selection, opens one serial handle with DTR/RTS disabled, and
never kills an existing monitor or probes the port using another handle.
The user must close the monitor and wake a sleeping device before download.

Every block uses a fresh token and a complete response deadline; unrelated
runtime lines cannot extend that deadline. The host checks numeric bounds,
revision, exact offset, complete-chunk CRC and the initially pinned file length.
A unique `CreateNew` partial file stays in the destination artifact directory.
Only after the transfer, flush and resource close succeed does it replace the
destination. Failures remove only that command's partial file and preserve
the previous destination; cleanup failures retain their own real diagnostics.

The ordinary monitor now uses `--no-reset` and forwards its baud explicitly.
`-ResetBeforeMonitor` requests ESP-IDF's startup reset when intentionally needed.
Its inventory wait never opens a probe handle or stops an existing monitor.

This implementation has static review only. No build, suite, simulator, flash,
download, monitor, SD mount, standby or physical boot behavior was validated.
