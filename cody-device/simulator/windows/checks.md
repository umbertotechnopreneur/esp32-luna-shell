[Desktop simulators](README.md) / Shared contracts and checks

# Shared native contracts, fixtures and checks

The native graphics driver (`cody_host_graphics.dll` on Windows and
`cody_host_graphics.so` on Linux) includes logic modules shared with firmware.
Python retains the window, transport, authentication and synthetic inputs; it does
not replace a missing or outdated library with a second implementation.
Loading verifies the manifest, source hashes and ABI.
Rounded controls use immutable vector icon masks and refined corner coverage.
The font renderer accepts the format explicitly recorded by each font package;
current committed glyphs remain 2bpp. The updated scene generator can bake 4bpp
with 4x supersampling after separately authorized generation with the licensed
source font. A library rebuild alone does not upgrade the committed glyph assets.
Host ABI 2 transfers all actions in a frame through one native call:
RGB is converted to RGB565 once and converted back after the complete raster pass.
The image is published only if the entire group succeeds. Snapshots also use a
single import with aging and one grouped metadata read; names, types, limits,
duplicates and errors are checked at the native boundary. A failed field discards
the candidate without changing the previous owner.
Value parsing preserves existing rules: this change reduces Python/C++ calls,
while timing and memory still require measurement. After changing these modules,
prepare the driver through the public console when execution is authorized:

```powershell
.\scripts\device.ps1 generate -Generator cody-simulator-graphics
```

| Path | Production code executed on the PC |
| --- | --- |
| CCP | EVENT decoder and transactions: IDs, original deadline, selection, reply, closure and rejection of reopening. |
| Questions | Presentation plan, routes, text, palettes, icons, action preparation/raster, reveal timing and touch admission. |
| Standby | Service policy, resource ordering, errors, rollback and recovery; resource operations remain synthetic. |
| Snapshot and HTTP | RTC decoder, aging with original timestamps, validity, redaction and the firmware JSON serializer. |

The `standby:profile` and `standby:fault` actions configure, respectively, the four
boolean requirements `deep_sleep_enabled`, `retained_connection`, `voice_listening`,
`touch_wake_available`, and a fault with `resource`, `target`, `error`, plus optional
`count`/`skip`. `press:pwr` runs the C++ transition and also publishes the result of
incomplete recovery. `rtc-registers` receives `registers`, a list of seven bytes,
and passes the values to the real RTC decoder. A decoding error remains visible
without replacing the historical sample.

Unmodeled groups remain unavailable. The ESP-IDF Task-WDT does not exist on the PC:
`/api/v1/health` reports unavailable liveness with `ESP_ERR_NOT_SUPPORTED` and
`ok=false`. Data remains **SYNTHETIC**; JSON adds
`simulator.runtime=production-cpp`. Selecting deep sleep is a policy decision,
not evidence of electrical sleep, power consumption or wake behavior.

Luna fixtures in `scenarios/` use `LUNASHELL.simulator.scenario.v1`: typed patches
and events ordered in virtual time. Native-device and controlled Studio fixtures
use different contracts; see [scenario formats](architecture.md#scenario-formats). Duplicate keys, incorrect types and inconsistent states are
rejected. Formatting and calibration retain their own preconditions.

```powershell
.\scripts\device.ps1 simulator-luna -ToolArguments @('--list-scenarios')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--scenario', 'healthy', '--at-ms', '2500', '--dump-snapshot')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--scenario', 'healthy', '--action', 'start:wireless', '--advance-ms', '1000', '--dump-api', '/api/v1/status')
```

`--scenario-file` selects an explicit fixture; `--output` writes under `artifacts/`.
Replay advances only when requested, preserving immutable generations.
The scenario list comes from the catalog rather than being duplicated here.

The internal `fake_device_server.py` server uses loopback, normally port 18080,
and synthetic/no-cache headers. Health is public; other GET requests require an
exact Bearer token, with a limit after twenty errors in sixty seconds.
The public fixture token is `LUNASHELL-SIMULATOR-ONLY-NOT-A-SECRET`.
The separate `/__simulator/` control handles load/advance/action; contracts are
documented in [HTTP device API](../../../docs/bsp/services/http-device-api.md).

Prepare these checks through the console, then request the authorization required
by [AGENTS.md](../../../AGENTS.md) before each execution:

```powershell
.\scripts\device.ps1 check -Check cody-ccp
.\scripts\device.ps1 check -Check cody-ccp-simulator
.\scripts\device.ps1 check -Check cody-character-studio
.\scripts\device.ps1 check -Check cody-runtime
.\scripts\device.ps1 check -Check cody-preview-raster
.\scripts\device.ps1 check -Check simulator
```

The two CCP suites do not open windows or send hooks to Codex. The simulator suite
includes real UDP on temporary loopback ports and isolated synthetic state, with
no LAN broadcast or access to personal associations. It requires the current
`cody-simulator-graphics` driver generated in this checkout before the suite runs.
The character suite runs the real C++ direction binding, saves labelled synthetic
frames and exercises a withdrawn Tk window with its scheduled renderer. Prepare
both `cody-simulator-graphics` and `cody-simulator-audio` through the console first.
It checks gaze, accepted caress warmth, matching compaction completion, source
gaps and Debug isolation; it does not connect to a physical Cody or live Codex.
The raster suite compares pixels with the per-pixel compositor and an independent
coverage oracle on small masks. The Luna suite covers fixtures, actions, redaction
and loopback transport. Current acceptance gates are in [TODO.md](../../../TODO.md).
For interpreters, paths and packaging, see the [console](../../../scripts/README.md);
for the device, see the [runtime](../../../docs/cody/runtime.md) and
[rendering](../../../docs/cody/runtime.md#experiments-and-source) contracts.

`cody-runtime` includes regression definitions for the shared C++ modules;
`simulator` includes aging, RTC, publication errors and native JSON. The presence
of these definitions does not establish that they were executed. Suites loading
the library require an up-to-date driver before they start.
