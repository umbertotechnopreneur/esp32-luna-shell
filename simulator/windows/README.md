# QSee.ai Windows device simulator

This desktop target mirrors the round 466 x 466 UI contract and application
flow. It does not emulate the Waveshare electrical hardware or ESP-IDF drivers.

Run from the project root:

```powershell
.\simulator\windows\run-simulator.ps1
.\simulator\windows\run-simulator.ps1 -Scenario sd-unformatted
.\simulator\windows\run-simulator.ps1 -Scenario gps-searching
.\simulator\windows\run-simulator.ps1 -Screen settings
```

Capture the current synthetic home screen without touching a device:

```powershell
.\simulator\windows\run-simulator.ps1 -Scenario healthy -Capture .\simulator\windows\healthy.png
.\simulator\windows\run-simulator.ps1 -Scenario healthy -Screen settings -Capture .\simulator\windows\settings.png
```

Controls:

- mouse click: synthetic touch
- `Enter`: confirm
- `Esc`: back/cancel
- `C`: open confirmation
- `R`: reboot the synthetic device
- `F2`: show or hide the 320 x 320 safe-area guide

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
gyroscope, temperature, bias, and calibration-sample fields. The simulator
continues to use deterministic synthetic values.
