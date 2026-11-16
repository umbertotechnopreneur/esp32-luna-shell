[Desktop simulators](README.md) / Live native device

# Live native device

The default native-device window transports completed firmware-owner
frames instead of reconstructing pages. In that window, **F12** saves the
current unscaled 466 x 466 frame to `artifacts/simulator/captures/` with a
unique name. The capture is SIMULATED and records a frame revision only; it
does not claim a screen identity or physical-device acceptance. The native
runtime and complete-screen atlas are still under integration. The Simulator
menu opens the battery, network and clock input editor: Read retrieves the
current projection, Apply publishes it, and Open/Save use JSON files. All
these fields are explicitly simulated; they are not measurements from a device.
The Display menu offers proportional fit or stretching to the available window.
Both use nearest-neighbour sampling of the native frame, without smoothing;
touch follows the displayed frame independently on each axis.

For a fresh, explicitly SIMULATED first boot, use the default native scenario:

```powershell
.\scripts\device.ps1 simulator
```

This scenario provides no physical hardware measurements. `settings_path: null`
selects a new uniquely named host-only NVS file under `artifacts/simulator/`
for each invocation; previous settings and the physical device are not erased.
With `use_pc_lan: true`, the PC's configured IPv4 LAN is read once at startup
without sending packets or reading credentials, and projected as a simulated Cody
connection labelled `SIMULATED-PC-LAN`. This does not establish Internet access
or the physical device's connection state. If the host configuration cannot be
read, the initial PC-sourced connection observation remains unavailable.
The same scenario explicitly enables one fictional open network, `SIM-CODY`,
for the onboarding scan and sets the simulated battery to 80 percent. Its
access-point identity, signal and connection are synthetic; no network scan
or connection is performed on the PC. Selecting it updates only the simulator.
`use_pc_clock: true` supplies the PC time and UTC offset to the simulated clock,
independently of the simulated battery configuration.
Additional first-boot scenarios are `cody-native-wifi-secured.json`,
`cody-native-wifi-empty.json`, and `cody-native-wifi-timeout.json` in
`scenarios/`. The protected synthetic network accepts the demo password
`CodyDemo42`; any other password produces a connection failure. The timeout
scenario returns a simulated timeout. BLE and Codex pairing scenarios are
also synthetic: no real radio operation or network connection is performed.
An unavailable native service remains unavailable, and bootstrap failures are
reported rather than bypassed. `-DeviceScenario` selects a different explicit
scenario. Preview-specific options select the separate
[legacy previews](preview.md).

Mouse input goes to the native touch owner. Press `b` for a short BOOT press,
`B` for a long BOOT press, and `F12` to save the completed native frame.
These keys belong to the live native window; the legacy preview has different controls.
