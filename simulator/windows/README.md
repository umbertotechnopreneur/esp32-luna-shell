# QSee.ai Windows device simulator

This desktop target mirrors the round 466 x 466 UI contract and application
flow. It does not emulate the Waveshare electrical hardware or ESP-IDF drivers.

Run from the project root:

```powershell
.\simulator\windows\run-simulator.ps1
.\simulator\windows\run-simulator.ps1 -Scenario sd-unformatted
.\simulator\windows\run-simulator.ps1 -Scenario gps-searching
.\simulator\windows\run-simulator.ps1 -Screen settings
.\simulator\windows\run-simulator.ps1 -Screen storage
.\simulator\windows\run-simulator.ps1 -Screen pairing-intro
```

Capture the current synthetic home screen without touching a device:

```powershell
.\simulator\windows\run-simulator.ps1 -Scenario healthy -Capture .\simulator\windows\healthy.png
.\simulator\windows\run-simulator.ps1 -Scenario healthy -Screen settings -Capture .\simulator\windows\settings.png
.\simulator\windows\run-simulator.ps1 -Scenario healthy -Screen storage -Capture .\simulator\windows\storage.png
.\simulator\windows\run-simulator.ps1 -Scenario sd-unformatted -Screen storage -Capture .\simulator\windows\storage-format-required.png
.\simulator\windows\run-simulator.ps1 -Screen pairing-app-qr -Capture .\simulator\windows\pairing-app-qr.png
.\simulator\windows\run-simulator.ps1 -Screen pairing-device-qr -Capture .\simulator\windows\pairing-device-qr.png
.\simulator\windows\run-simulator.ps1 -Screen pairing-success -Capture .\simulator\windows\pairing-success.png
.\simulator\windows\run-simulator.ps1 -Screen pairing-failed -Capture .\simulator\windows\pairing-failed.png
.\simulator\windows\run-simulator.ps1 -IdleState command-hidden -Capture .\simulator\windows\command-hidden.png
.\simulator\windows\run-simulator.ps1 -Screen settings -IdleState fullscreen -SafeGuide -Capture .\simulator\windows\settings-fullscreen.png
.\simulator\windows\run-simulator.ps1 -IdleState dimmed -Capture .\simulator\windows\dimmed.png
.\simulator\windows\run-simulator.ps1 -Screen chrome-settings -Capture .\simulator\windows\chrome-settings.png
.\simulator\windows\run-simulator.ps1 -Screen power-settings -Capture .\simulator\windows\power-settings.png
.\simulator\windows\run-simulator.ps1 -Screen hardware -Capture .\simulator\windows\hardware.png
.\simulator\windows\run-simulator.ps1 -Screen tof -Capture .\simulator\windows\tof.png
.\simulator\windows\run-simulator.ps1 -Screen tof-settings -Capture .\simulator\windows\tof-settings.png
```

Controls:

- mouse click: synthetic touch
- `B`: simulate a short `BOOT` press. From `active` it toggles Fullscreen;
  from an automatic hidden/dim/off state it wakes to `active` first, and
  `standby` ignores it.
- `P`: toggle the synthetic `PWR` standby/awake state and cancel Fullscreen
- `Enter`: confirm
- `Esc`: back/cancel
- `C`: open confirmation
- `R`: simulate the three-second `BOOT` soft reset and cancel Fullscreen
- `F2`: show or hide the dynamic content-viewport guide without changing page

The capture-only `-IdleState` option accepts `active`, `command-hidden`,
`chrome-hidden`, `fullscreen`, `dimmed`, `off`, and `standby`. It provides
deterministic visual checks for the power-management stages. A synthetic touch
restores an automatically hidden stage, but Fullscreen remains locked while
page hit targets stay active. Standby ignores touch and short `BOOT`; it wakes
only through synthetic `PWR`.

The viewport follows the firmware insets. With both arcs visible it is
320 x 320 at `y=73`; releasing only the lower Command Arc grows it to
320 x 393; hiding both arcs or entering Fullscreen grows it to 320 x 466 at
`y=0`. The `F2` guide shows the current bounds and the original safe square.
The Settings demo also shifts upward and reveals additional Display & Motion
controls as that viewport grows.

The persistent upper status ribbon and lower adaptive command arc reproduce the
firmware geometry, visual hierarchy, and semantic color roles. The Windows
renderer uses antialiased radial atmosphere, gradients, and restrained bloom so
captures remain representative of the AMOLED design rather than raw Tk canvas
primitives. The displayed values remain deterministic simulator-only data.

Build the standalone Windows executable:

```powershell
.\simulator\windows\build-exe.ps1
```

The build script creates an isolated `.venv-simulator` environment and writes
the executable to `simulator/windows/dist/QSeeDeviceSimulator.exe`.

## Audio scope demo

After the synthetic boot completes, click the Audio command. The page is a
high-refresh preview of the two-microphone layout. Press `Esc` to return. The
simulator bars are deliberately synthetic and visibly labelled; CPU utilization
is shown as unavailable rather than invented.

The firmware audio snapshot now carries two interleaved ES7210 channel levels,
while the firmware IMU page consumes calibrated QMI8658 acceleration,
gyroscope, temperature, bias, and calibration-sample fields. Home CPU bars use
real idle-task runtime on hardware; simulator CPU bars remain visibly labelled,
deterministic synthetic values.

The Distance and Distance Settings demos mirror the dual-TOF page, IMU gear
navigation, robust-window controls, 500–1500 mm reference slider, and
calibration action. Every simulated distance and configuration value is
explicitly labelled `SYNTHETIC`; the simulator never opens a serial port.
