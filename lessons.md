# Lessons

## Brookesia startup overlay

- The `SuperOS` banner in the boot video came from Brookesia System Super's
  startup overlay, not from the QSee app.
- The visible image is
  `third_party/esp-brookesia/system/brookesia_system_super/resource/startup/images/background.png`,
  loaded through the System Super startup overlay.
- To remove that banner, disable
  `CONFIG_BROOKESIA_SYSTEM_SUPER_ENABLE_STARTUP_OVERLAY` instead of changing
  the QSee boot screen.
- The QSee boot image and the Brookesia startup overlay are separate stages, so
  check both whenever a future video shows two branded frames.

## Single-app Brookesia runtime

- System Core is the application/runtime layer. System Super adds an optional
  Shell application with launcher, global overlays, keyboard, and dialogs.
- QSee is the only application on this device, so use System Core directly and
  do not stage, install, or start the System Super Shell.
- Removing the Shell also removes its keyboard and message-dialog
  implementations. Product flows that need them must use explicit QSee-owned
  declarative components; do not silently ignore those requests or reintroduce
  the Shell as a fallback.

## ESP-IDF 6 flash operations and PSRAM task stacks

- ESP-IDF 6 asserts if a task whose stack is in PSRAM performs an operation
  that disables the flash/PSRAM cache, such as an NVS commit.
- Keep Brookesia System Core's three 64 KiB scheduler workers in PSRAM. App
  startup synchronously posts GUI work, so one worker deadlocks waiting for its
  own scheduler. A 32 KiB internal Core worker also does not fit reliably after
  display and hardware startup because the remaining internal heap is
  fragmented.
- Route QSee settings through its fixed internal-stack NVS executor. Keep
  Brookesia's general service workers in PSRAM and use one internal secondary
  scheduler worker for the Storage service.
