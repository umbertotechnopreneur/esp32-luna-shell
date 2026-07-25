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
