# Windows simulators

I use these previews to explore the 466 × 466 circular UI and the behavior described
by the assets. **SYNTHETIC** and **SIMULATED** remain clearly visible: values
generated on the PC never enter firmware snapshots. Host FPS, Pillow images and
PC audio remain host observations; Cody's electrical behavior is verified on Cody.

## Open Cody Studio

I always use the public console from the repository root:

```powershell
.\scripts\device.ps1 simulator
.\scripts\device.ps1 simulator -Screen greeting
.\scripts\device.ps1 simulator -Screen boot-waking -Rotation 180
.\scripts\device.ps1 simulator -Screen boot-vibeware -AtMs 1500 -OutputPath artifacts/simulator/boot.png
.\scripts\device.ps1 boot-screens
```

Python, Pillow and PyYAML are required. The console prepares the host audio driver
before an interactive session; a static capture does not start audio.
The preview starts without a rig overlay. The **Face**, **Shape**, **Inputs** and
**Inspect** panels control expressions, outlines, synthetic stimuli and diagnostics.
Each tab retains its own scroll position; in narrow windows, the inspector moves
below the preview when vertical space permits.

Resizing affects presentation only: geometry, rotation and pointer input stay in
the display's logical coordinates. Per Monitor v2 is enabled before Tk, using the
monitor's initial DPI scale. Moving between monitors with different DPI still
requires a dedicated Tk transition. The default limit is 30 Hz, or 25 Hz in the
jelly study; the inspector offers 15, 25, 30 or 60 Hz. The footer distinguishes
observed FPS from the requested limit. Text readouts update at 10 Hz and status
at 5 Hz; model clocks follow elapsed time.

The VibeWare floppy uses 42 frames and the approved GIF's 3.5-second timeline.
Cody's native compositor centers `Cody AI Companion` above
`A distraction-free sidekick`, then renders the complete floppy at (137,164),
192 x 219, on black with shared rotation. It uses the same production decoder,
font preparation and row compositor as firmware. Generic Luna retains its
original branding placement.
`-AtMs` selects the frame for a capture; `boot-screens` includes the introduction.
The Manifesto link opens only after an explicit click.

## Connect the simulator to CCP

```powershell
.\scripts\device.ps1 simulator -Ccp
```

`-Ccp` also opens the **CCP** panel and a real UDP endpoint restricted to
`127.0.0.1:42117`. Select **Connect Codex** in the panel and read the code.
In Codex, explicitly request a connection to **Cody Studio SIMULATED**; discovery
adds `include_simulator: true`, then pairing uses the visible code.
For installation and trust, follow the [canonical CCP README](../../plugins/cody-codex-plugin/README.md).

The endpoint speaks CCP1 using the plugin's same primitives: authentic offers,
PAIR/PAIRED, CONFIRM/ACTIVE and FIND/READY/ACTIVITY. After PAIR, it saves a candidate
separately from the active association. CONFIRM or an authenticated READY answering
the current nonce promotes it; the latter path allows recovery after a restart
without a new code.
Every valid READY receives COMMITTED only after the active commit: the PC can
also complete its candidate if ACTIVE or an earlier COMMITTED was lost.
The protocol preserves the old record on both sides until the new commit is
proven; it does not promise an atomic transaction between PC and device.
After Cancel, the old link can resume, but the PC candidate remains explicit:
replacing it requires a new code and `replace_pending: true`.

The simulated identity starts with `02` and resides, alongside DPAPI-protected
associations, in `artifacts/cody-simulator-state/device.json`.
Build cleanup preserves this directory. A second endpoint cannot take the same
port; a read error remains an error instead of inventing a new device.
The code appears only in the UI, never in logs or the public snapshot.

The panel supports:

- confirming **Replace pairing** while preserving the old record until the new confirmation;
- using **Cancel code** to discard the current window or candidate;
- confirming **Forget pairing** to delete only simulator associations;
- disabling **SIMULATED LAN available** and restoring it without losing the identity;
- disabling **Show CCP on the display** to return to the rig while the link remains active.

CCP can explicitly request a restart or factory reset of this **SIMULATED** target,
using the distinct phrases and code described in the
[plugin README](../../plugins/cody-codex-plugin/README.md#riavvio-o-ricomincio-dal-setup).
A restart preserves the store; a factory reset deletes only simulator settings
and associations and leaves the simulated LAN unconfigured even after reopening.
To resume, enable **SIMULATED LAN available** and start new pairing.
The old PC record remains separate; nothing is deleted on the physical device,
in firmware or on the SD card.

The preview shows existing scenes/fonts and the state received from the broker.
Project names occupy at most two lines of 21 characters; activity appears only
after fresh packets arrive. After eight seconds without updates, activity is
treated as **UNAVAILABLE**. Chat changes follow the actual prompt sent to Codex;
there is no local selector or fabricated hook producer.

CCP 0.6 also supports authentic EVENT messages. A question follows 26 → 27 → 31:
open it, select a card, review its description and press **Send**.
The selection stays local and reversible until Send; Back preserves the path.
A UDP ACK alone leaves **Confirming delivery**: **Reply sent** requires the second
recipient-confirmation poll within the 180-second TTL. Disabling LAN in the panel
keeps the same question visible, with sending disabled until a new authenticated
snapshot arrives.

PR views display data only. **View failures** opens up to six complete names,
three per page, with vertical scrolling and Back navigation. Rows are not buttons;
merge, retry, logs and stack traces are not offered. New notifications replace old
ones without interrupting a question. Sending tools require
`include_simulator: true` even after pairing; **SIMULATED** stays visible, without
presenting the preview as firmware.

This mode exercises the protocol path and its host presentation.
Commissioning is controlled from the desktop panel: it does not run ESP-IDF
firmware here or reproduce every gesture and screen of native onboarding.
To avoid ambiguous captures, `-Ccp` is supported only in interactive `rig` mode,
without material studies. Physical Cody remains an independent identity.

## Expressions, symbols and materials

The Face panel selects a manga symbol for the `rig` presentation.
The semantic `sleep` and `sleeping` presets activate Sleep; closing the eyes with
a slider does not mean falling asleep. A manual face change removes the preset's
identity. Greeting, boot and Micro do not display the symbol.
To explore automatic petting, disable the diagnostic rig and select None.

IDs, priorities, timing, gestures and mood come from `assets/cody/personality.yaml`.
The channel waits for a stable request for 300 ms, enters over 400 ms, remains for
at least 2,500 ms and disappears over 600 ms. Semantic Sleep skips the initial
wait and can immediately release the competing symbol; a manually selected
Sleep symbol retains the normal wait. Pause freezes active time.
This does not promise that the physical panel stays on until the fade completes.

The anchor follows Nose with a 360-ms window; scaling uses 400 ms.
There are 17 fixed intervals of 25 ms, a maximum radial delay of four pixels and
maximum deformation of two pixels. Geometry history does not queue symbols.
Palette, coverage and opacity come from generated RGB565 assets.
Back and the status region remain reserved: if a symbol does not fit, the result
is `BLOCKED_BY_LAYOUT`, without unexpectedly shrinking or moving it.
The complete contract is in [personality icons](../../docs/architecture/cody/personality-icons.md).

Three studies remain separate for the cloud's appearance:

| Option | Study |
| --- | --- |
| `-CloudJellyStudy` | Pearl scanline approach, interactive or PNG/GIF; the face follows the body. |
| `-CloudMaterialStudy` | Host volume study with static capture. |
| `-CloudPngMeshStudy` | Texture and triangular mesh; `-MeshOverlay` displays the topology. |

Select one material at a time. PNG is preferable for assessing colors;
GIF follows a fixed synthetic timeline rather than sensor replay.

```powershell
.\scripts\device.ps1 simulator -CloudJellyStudy
.\scripts\device.ps1 simulator -Screen greeting -CloudJellyStudy -AtMs 1500 -OutputPath artifacts/simulator/greeting.png
.\scripts\device.ps1 simulator -CloudPngMeshStudy -MeshOverlay -OutputPath artifacts/simulator/cloud-mesh.gif
```

## Luna and Leg previews

```powershell
.\scripts\device.ps1 simulator-luna -ToolArguments @('--scenario', 'battery-low', '--screen', 'battery')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--screen', 'settings', '--capture', 'artifacts/simulator/settings.png')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--board-config')
.\scripts\device.ps1 simulator-luna -ToolArguments @('--screen', 'leg-motion')
.\scripts\device.ps1 package-simulator
```

Luna provides diagnostic pages, status/command arcs, power behavior and semantic
actions. `--board-config` selects the minimal shell from the board YAML and prepared
shell document; it is not combined with full-device scenarios, pages or radios.
Leg retains accelerometer, gyro, combined, motion and settings views, all synthetic.

The EXE package remains Luna under `artifacts/simulator/windows/dist/`.
Packaging explicitly prepares the host environment; opening a preview does not
silently install dependencies.

| Luna control | Synthetic action |
| --- | --- |
| Mouse | Touch |
| B | Short BOOT press / fullscreen |
| P | PWR standby |
| R | BOOT software reset |
| Enter / C | Confirm |
| Esc | Back |
| F2 | Viewport help |

In standby, touch and short BOOT presses are ignored; synthetic PWR wakes the device.
The viewport starts at 320 × 320 with y=73; hiding the lower arc extends it to
320 × 393, and hiding both arcs/fullscreen extends it to 320 × 466 with y=0.
`--idle-state` can capture active, command-hidden, chrome-hidden, fullscreen,
dimmed, off and standby states.

## Fixtures and checks

The `cody_host_graphics.dll` driver also includes logic modules shared with firmware.
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

`scenarios/` uses `LUNASHELL.simulator.scenario.v1`: typed patches and events ordered
in virtual time. Duplicate keys, incorrect types and inconsistent states are
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
documented in [HTTP device API](../../docs/architecture/http-device-api.md).

Prepare these checks through the console, then request the authorization required
by [AGENTS.md](../../AGENTS.md) before each execution:

```powershell
.\scripts\device.ps1 check -Check cody-ccp
.\scripts\device.ps1 check -Check cody-ccp-simulator
.\scripts\device.ps1 check -Check cody-runtime
.\scripts\device.ps1 check -Check cody-preview-raster
.\scripts\device.ps1 check -Check simulator
```

The two CCP suites do not open windows or send hooks to Codex. The simulator suite
includes real UDP on temporary loopback ports and isolated synthetic state, with
no LAN broadcast or access to personal associations.
The raster suite compares pixels with the per-pixel compositor and an independent
coverage oracle on small masks. The Luna suite covers fixtures, actions, redaction
and loopback transport. Results and limitations are recorded in
[done.md](../../done.md) and [TODO.md](../../TODO.md).
For interpreters, paths and packaging, see the [console](../../scripts/README.md);
for the device, see the [runtime](../../docs/architecture/cody/runtime.md) and
[rendering](../../docs/architecture/cody/rendering.md) contracts.

`cody-runtime` includes regression definitions for the shared C++ modules;
`simulator` includes aging, RTC, publication errors and native JSON. The presence
of these definitions does not establish that they were executed. Suites loading
the library require an up-to-date driver before they start.
