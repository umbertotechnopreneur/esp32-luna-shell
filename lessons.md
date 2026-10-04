# Recurring engineering lessons

These are observed pitfalls, not a second instruction contract or proof of device
validation. [AGENTS.md](AGENTS.md) owns policy; [TODO.md](TODO.md) owns open work.

## Ownership and truthful evidence

- Request admission, credentials, a successful strip write or watchdog progress
  is not operation completion. Pairing needs observed backend/session outcomes;
  display transitions need a retired complete frame; optional faults stay real.
- Keep canonical snapshot ownership/timestamps through source splits. Reuse each
  fresh diagnostic sample at most once; never create a parallel sensor cache.
  Coalesce duplicate logs only: immediate error, counted 60-second reminder,
  immediate recovery. Do not hide errors or slow healthy acquisition.
- A working-device report or a capture started after failure does not identify
  an intermittent trigger. A flashed/ELF mismatch invalidates symbolic diagnosis.
  Compare complete-frame/stage costs, payload and CPU together; a higher frame
  rate alone is not lower CPU. Establish an actually static route baseline.
  [Response and rendering](docs/architecture/cody/rendering.md).

## Console and storage

- Monitor can reset a continuously connected MCU; passive attach must not probe
  the port. COM numbers/list order are not identities. Classify missing ports
  by native codes, preserve exception/exit causes and acquire artifact ownership
  before diagnostic writes; secondary logging failure is separate.
- Capture before best-effort USB output. Release storage/logger locks before USB
  transmission. File revisions/CRCs prove transfer identity, not a freeze's cause.
  [Offline logging](docs/architecture/offline-logging-usb.md).

## Build graph and GUI lifetimes

- ESP-IDF resolves components before Kconfig: variants need admitted roots,
  defaults and reviewed build-local locks. Optional-requires cannot discover
  excluded roots. Gate SDK headers too; codec admission does not enable boot WAV.
- Keep fresh build roots empty until IDF creates them; generated assets/stamps
  belong to each build, not shared source staging. Debug/Release is independent
  of hardware composition; existing Release output is not user intent.
  [Build modes](docs/architecture/firmware-build-modes.md).
- System Core is the app runtime, not System Super startup overlays/services.
  One lazy route owns its document/subscriptions/maps; unload releases captures,
  and callbacks are destroyed outside registry locks. On partial failure retain
  the real error and clean/restore explicitly. Intern only static styles.
- Bindings write final values, apply coalesced backend masks, then notify. No-op
  values need no batch. Declarative transparency/popup styling must follow the
  actual schema/backend, not invented properties.
  [Architecture](docs/README.md).

## Cache, memory and frame ownership

- PSRAM-stack tasks cannot safely commit NVS/cache-disabling flash operations.
  Use the internal-stack settings/storage executors; do not shrink worker stacks
  without measurements. Delete capability-allocated tasks with the matching API.
- Bound logger/scratch together and publish raw heap/block/allocation failures.
  Allocation-failure hooks must neither allocate nor log. Use bounded arithmetic;
  long active clocks need an explicitly wide, saturating representation.
- Coverage antialiasing adds nested raster scratch even for small cheeks. Cody's
  compiled worker/frame/coverage chain reached 8,176 bytes and overflowed an
  8 KiB worker while startup's 12 KiB main stack still drew successfully.
  Budget the full call chain and retain measured worker headroom in Debug.
- A frame owns its transient internal DMA lease and watchdog from geometry to
  completion. Quarantine uncertain DMA completion even after deadline; a partial
  frame invalidates comparison history. Endian conversion consumes native pixels.
  Commit previous geometry only on success and erase both old/new bounds.
- CO5300 QSPI cannot safely use direct PSRAM DMA here. Bounded internal bounce
  strips preserve completion ownership; smaller tiles must not create unbounded
  deferred touch work. Reserve BLE contiguous headroom before Wi-Fi.
  [Mitigations](docs/architecture/display-touch-runtime.md),
  [rendering](docs/architecture/cody/rendering.md).
- On Cody, reserve the BLE controller's contiguous DMA lease before SD/display
  fragmentation, then drain and briefly park frame-owned display DMA during
  Wi-Fi/BLE startup. Keep BLE's free-space and largest-block preflight strict;
  aggregate free RAM alone does not prove a controller-sized block exists.

## Power, wake and audio

- Complete Sleep before closing display writes; prepare Wake while dark before
  panel exposure. Lock display/brightness/rotation before settings, defer
  rotation while off and require fresh orientation before replay.
- Software standby retains route/active clocks/paused pose; lifecycle presentation
  must not overwrite that state. Reconcile actual backend changes before resume.
  Real Deep-sleep resets RAM/owners/sessions; no fabricated session restoration.
- Transient scene timeout policy must not persist global settings or refresh
  activity every frame. Positive pairing overrides also suppress early dim.
- Typewriter audio follows retired visible-prefix deltas, not evaluated frames.
  Replacement/scroll/resume silently re-prime; cancellation and pending payload
  must share one coherent generation/mailbox. Admission does not prove playback;
  use existing codec/quiet/volume ownership and retain real async failures.
  [Runtime and scenes](docs/architecture/cody/runtime.md).
- Standby quiesces producers before consumers; wake restores consumers dark,
  then producers before exposure. Failed transitions roll back or remain explicitly
  inconsistent. Diagnostic microphone handoff rechecks ownership under the lock.
- PWR is PMU PKEY, not SD GPIO41. PMU/RTC interrupts lack a direct ESP wake route;
  touch GPIO11 and IMU INT2/GPIO21 are separate RTC candidates. QMI WoM needs
  revision-aware CmdDone/ACK and explicit exit; release RTC holds/mux after
  rejected sleep. Keep touch wake scanning alive.
- Keep unsafe IDF 6.0.2 USB SOF and unavailable PHY-retention hooks disabled.
  Native USB does not survive Light-sleep; CPU sleep figures are not board
  current. Powered GPS/shared rails prevent claimed Backup savings; verify
  codec shutdown physically. Retain the measured 80 MHz floor pending PWR tests.
- RTC oscillator-stop remains unavailable until trusted write/readback. LC76G
  I2C control timing/terminal ACK parsing and bounded failed-read backoff matter
  to shared-lock responsiveness. Polling slower alone does not power down IMU.
  [Deep-sleep](docs/architecture/release-deep-sleep.md),
  [low-power](docs/architecture/low-power-management.md),
  [hardware](hardware-inventory.md).

## Watchdogs

- A software timeout cannot terminate a wedged SDK call. Its worker keeps buffers/
  Task-WDT ownership until actual exit; recoverable deadlines need margin below
  Task-WDT. Fast repeated failures are errors, not success or necessarily a hang.
