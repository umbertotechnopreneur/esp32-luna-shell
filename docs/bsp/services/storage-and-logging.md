# Offline logging and native USB readback

The canonical logger captures diagnostics before best-effort native USB output.
The removable-storage owner alone mounts and protects the SD lifetime; the
logger never formats or mounts a card. Cody's profile admits this existing
owner with FAT long filenames. The common firmware defaults select native USB
Serial/JTAG as the console so every supported profile has the explicit VFS
dependency required by the logger's USB driver owner.

## SD startup clocks

After SDMMC slot initialization, the BSP selects the official 400-kHz probing
frequency and queries its calculated frequency before initializing the card.
The latter official host operation also prepares the clock-source identity in
ESP-IDF 6.0.2. It is not a physical frequency measurement. Card initialization
still negotiates the later supported runtime speed.

An unresolved frequency makes this SDK reprogram the clock before every early
command, re-enabling low-power clock gating even after ACMD41 requested
continuous clocking. Explicit probing selection prevents that overwrite.
Espressif's [ACMD41 clock correction](https://github.com/espressif/esp-idf/commit/7524f40)
documents the continuous-clock requirement. Clock-selection/query failures
retain their original error and slot-initialization stage; ordinary staged
cleanup remains responsible for the attempt. Runtime verification is recorded
in the root `done.md` and open limits in `TODO.md`.

The corrected device capture no longer reprograms the frequency before every
command, but ACMD41 remains busy: this correction is insufficient to resolve
the startup fault. A continuous-clock request log and calculated frequency do
not establish the physical CLK waveform. Debug automatic light sleep is off;
the current evidence does not justify a power-management change.

An [ESP32-S3 reset report](https://github.com/espressif/esp-idf/issues/11438)
describes a working card failing OCR after reset, with one-bit SDMMC and external
pull-ups, on IDF 4.4.4. It is an analogy, not the same verified cause. A
[first-person STM32 report](https://www.mikrocontroller.net/topic/424234#6502446)
describes indefinite ACMD41 busy despite repeated CMD0 after an initial power-up
sequence; waiting before the first command avoided it there. Our first attempt
already occurs seconds into startup, so that report does not justify adding an
arbitrary delay. No matching confirmed Waveshare resolution was found.
The next discriminating evidence is simultaneous SD VDD and CLK/CMD observation
across the known working-to-reset transition; unchanged reset loops or further
card swaps do not distinguish power disturbance from actual clock loss or
retained card state. No shared power rail is cycled by this implementation.

## Recording and retention

Application startup installs the canonical hook, starts its native USB receiver,
and records a `BOOT START` marker with the reset reason and firmware version,
including in Release. Normal log output uses `usb_serial_jtag_write_bytes` with
zero wait. An absent or non-consuming terminal can lose USB output; its measured
drop counter is exposed by `LUNASHELL_logger_get_sd_status` separately from real
filesystem errors. The physical boot/display problem reported by the user has
not been verified as resolved.

The SD profile temporarily allocates 32 KiB of internal RAM for its first 128
captured records. The callback's live ring, format slots and zero-wait queue also
remain internal; it never allocates, waits for capacity or accesses a PSRAM
archive. Profiles with SD disabled do not allocate the boot backlog. Headless startup
starts the asynchronous storage owner after initial hardware acquisition and
before display/Cody startup. Once SD is mounted and mirroring is enabled, the
writer replays early records in batches of at most eight, confirms each flush,
then consumes the ordinary queue. Confirmed final replay detaches and frees the
backlog. A failed batch keeps its unconfirmed records; an explicitly restarted
mirror may repeat that uncertain batch. This is not exactly-once persistence.

Both startup paths seal boot capture and notify the writer without waiting for
file I/O. If a backlog remains, that sole writer moves the sealed records into
an exactly sized PSRAM archive and releases the 32-KiB internal allocation, even
with SD absent or mirroring disabled. Copying/allocation happen outside the log
callback and critical sections. Allocation failure retains the original records
and real error, without an allocation retry loop or stopping the device. Replay
later frees the archive only after its final confirmed flush. All retention is
volatile and is lost on reset or power loss.

Format-slot exhaustion and a full queue count losses without disabling a healthy
SD mirror. Truncation, boot overflow and USB drops have separate counters. Actual
SD write/flush/notification failures still disable the affected sink and retain
their original errors. Logger initialization is an explicitly optional startup
stage: its real failure is returned and published without aborting Cody/UI startup;
repeat initialization and failed status queries retain the original cause.

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
remain explicit limits. A successful stdio flush is not a guarantee against
filesystem/card corruption from power loss. No missing/corrupt preference or filesystem is repaired
implicitly, and no removable card is formatted by logging or readback.

## Read ownership and integrity

The native USB receiver accepts only fixed current/previous identities. It
calls an immutable callback registered by the storage owner. Each read holds
the storage mutex, rejects an unmounted/terminal/standby state, then takes the
logger I/O mutex shared by writing, closing, rotation and readback. Only the
bounded filesystem operation holds those ownership guards. Optional logger I/O
does not subscribe a watchdog that resets the whole device. The registered owner
hooks preserve the original operation result. Both locks are released before
sending a response over USB. Stop/ownership waits remain bounded; a wedged driver
cannot be forcibly cancelled or safely unmounted while ownership is uncertain.

The canonical acquisition owner samples `storage.logger_capture` on every Full
Snapshot refresh, independently of SD mount state. It publishes original query
timestamps, all five loss counters, the last capture error/time, and retained
internal/PSRAM boot bytes. Counter-query validity and SD mirror health are separate:
valid counters do not claim that every log was delivered. Failed queries retain
the previous observation with invalid/stale state. Consumers never query logger
state directly. Cumulative losses produce a warning on first observation and at
most once per 60 seconds, instead of a recursive warning for every dropped line.
Optional capture publication uses a zero-wait snapshot lock and returns its real
failure; required component publication keeps its existing fatal invariants.

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

The 2026-10-05 PSRAM/best-effort revision has static source review only. No new build, suite, simulator, flash,
download, monitor, SD mount, standby or physical boot behavior was validated.

## Storage lifecycle

`/littlefs` is an internal flash volume containing staged Brookesia and
application resources. The official Brookesia HAL adaptor mounts it and
provides the NVS-backed key-value interface, with mount-time formatting
disabled. `/sdcard` is the removable FAT volume owned by the firmware for the
mirrored canonical log and user data. The HAL adaptor's SD-card backend stays
disabled; neither volume is used as a fallback for the other.

The board has no SD card-detect pin, so a failed SDMMC probe is reported as
`card/transport unconfirmed`, never as confirmed physical absence. Only
explicit transient card/transport and filesystem failures are retried, and
those probes never format the card. `format-required` is published only after
card initialization and an exact `FR_NO_FILESYSTEM` result; probing then pauses
until a matching explicit authorization arrives.

The destructive dialog can authorize one immediate retry only. That permission
is bound to all observed CID fields plus capacity and is consumed even if the
card changes, disappears, fails transport, or no longer needs formatting.
Cancelling the dialog performs no storage write. After a successful mount, the
storage task polls card status; removal or transport failure invalidates and
unmounts the volume before retrying. Internal, destructive, poisoned, or
incompletely cleaned failures stop retries for the current boot and remain
visible as terminal cleanup/ownership errors.
