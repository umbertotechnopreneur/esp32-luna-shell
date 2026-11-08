# Removable storage

Owns card acquisition, mount/health lifecycle, filesystem observations and explicit
format requests. The stub reports storage excluded by the selected build.

Internal settings and removable SD remain separate owners; card formatting is never
an implicit recovery step.

`CONFIG_LUNASHELL_STORAGE_DEBUG_PROBE` enables the requested one-second SD
diagnostic by default in Debug builds with removable storage; Release excludes
the probe. The existing acquisition task owns every status command and file
operation. Each nominal awake second, a responding mounted card receives a
sequence/timestamp record of less than 96 bytes in
`/sdcard/LUNASHELL/.sd-debug-probe`, followed by `fsync`, exact readback comparison
and close. This verifies the firmware filesystem path, not power-loss durability.
The file is overwritten, never appended, and no other file is a probe target.

Three `SD DEBUG` lines per heartbeat separate lifecycle state, last card status
and last write result. They fit the canonical logger's 256-byte record slots.
The owner publishes the same observations under `storage.debug`, including
actual I/O timestamps, stages, byte counts, errno and the last verified-write
time. Standby, performance suspension, unavailable media and terminal ownership
prevent write probes. A failed close blocks further diagnostic writes for this
boot rather than guessing descriptor ownership; its real error remains visible.
Diagnostic failures do not substitute for the normal mount/recovery outcome.

Suspended and retry waits still emit state heartbeats, preserving their original
notification/backoff deadlines and quiescence acknowledgement. These heartbeats
use the wait's tick clock to avoid skipping a second due to microsecond jitter;
their published timestamps still come from the actual observation time. They
perform no SD I/O and retain the previous I/O timestamps. `status_age_ms` and
write `age_ms` identify old observations; `UNAVAILABLE` means no actual sample.
Synchronous SDK/file calls can delay a heartbeat: the owner does not invent
samples or run a second sensor reader to conceal an I/O stall.
Each I/O result also carries `sample_attempt`, the mount attempt that owned it,
so a result from an earlier card/mount is distinguishable after reinsertion.

The configured bus is 1-bit CLK/CMD/D0 on GPIO2/GPIO1/GPIO3 and uses
`SDMMC_SLOT_NO_CD`. `cd=NC` therefore means no card-detect input is configured;
it does not claim a physically absent card. Presence is established only by
successful protocol communication. The local hardware inventory also records
that this board has no SD card-detect pin.

Wake logs report `storage_resume`, the service transition result. Only canonical
mounted/status/write observations establish actual SD availability; successful
service resumption does not establish that an unmounted card has initialized.
