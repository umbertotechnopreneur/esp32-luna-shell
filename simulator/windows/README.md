# QSee.ai Windows device simulator

This desktop target mirrors the round 466 x 466 UI contract and application
flow. It does not emulate the Waveshare electrical hardware or ESP-IDF drivers.

Run from the project root:

```powershell
.\simulator\windows\run-simulator.ps1
.\simulator\windows\run-simulator.ps1 -Scenario sd-unformatted
.\simulator\windows\run-simulator.ps1 -Scenario gps-searching
```

Controls:

- mouse click: synthetic touch
- `Enter`: confirm
- `Esc`: back/cancel
- `C`: open confirmation
- `R`: reboot the synthetic device
- `F2`: show or hide the 320 x 320 safe-area guide

Build the standalone Windows executable:

```powershell
.\simulator\windows\build-exe.ps1
```

The build script creates an isolated `.venv-simulator` environment and writes
the executable to `simulator/windows/dist/QSeeDeviceSimulator.exe`.

## Audio scope demo

After the synthetic boot completes, click `OPEN AUDIO SCOPE`. The page is a
black, high-refresh preview of the two-microphone layout. Press `Esc` to
return. The simulator bars and CPU percentages are deliberately synthetic;
the firmware page reads memory, task, priority, stack watermark, and audio
health from `DeviceSnapshotService`. CPU utilization remains explicitly
unavailable because runtime statistics are disabled in the current
configuration.
