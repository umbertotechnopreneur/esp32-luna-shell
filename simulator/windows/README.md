# Desktop simulators

I use these previews to explore the 466 × 466 circular UI and the behavior described
by the assets. **SYNTHETIC** and **SIMULATED** remain clearly visible: values
generated on the PC never enter firmware snapshots. Host FPS, Pillow images and
PC audio remain host observations; electrical behavior requires device evidence.

## Open Cody Studio

I always use the public console from the repository root:

```powershell
.\scripts\device.ps1 simulator
.\scripts\device.ps1 simulator -Screen greeting
.\scripts\device.ps1 simulator -Screen boot-waking -Rotation 180
.\scripts\device.ps1 simulator -Screen boot-vibeware -AtMs 1500 -OutputPath artifacts/simulator/boot.png
.\scripts\device.ps1 boot-screens
```

Cody Studio runs from source on Windows and Linux with PowerShell 7, a native C++20
compiler, Python with Tk and Pillow ImageTk, and PyYAML. On Ubuntu, the host packages
include `g++`, `python3-tk`, `python3-pil.imagetk` and `python3-yaml`. Use a graphical
session for the interactive window. Host audio uses ALSA `aplay` from `alsa-utils`.
The console builds the host audio driver before interactive launch. Static captures
use the graphics driver and do not start audio.

```bash
pwsh -NoProfile -File ./scripts/device.ps1 simulator
pwsh -NoProfile -File ./scripts/device.ps1 simulator -Ccp
pwsh -NoProfile -File ./scripts/device.ps1 simulator -Screen rig -OutputPath artifacts/simulator/cody.png
```

The default Cody Studio opens the device preview. The existing VibeWare floppy
plays once, then first run opens **Connections**. A retained simulator association
opens **Device**. **BOOT / Options** opens the device menu. **PWR / Standby** toggles
the simulated display, and **Simulate shake** returns the carousel to Device.
The dashboard and Options page arrows fade after ten seconds. A touch reveals
them again. **Options 1/2** contains **Connections**, **Codex stats** and
**Visible screens**. The right arrow opens **Options 2/2** with **Display** and
**Behavior**. Back from either category returns to the second Options page.
**Go home** on either page opens Reset countdown or the first enabled statistics
screen, with Device as the fallback. Brightness, orientation and visibility
choices affect this preview session. **About** shows Studio uptime and the next
page has a scannable project QR code and an explicit **Open link** action.

The ten-page carousel has Device, Reset countdown, Codex limits, Today,
Last 7 days, Token history, Credits, Codex account, Context and About, plus the
About QR page. The loopback endpoint supplies authenticated link and Context
observations. Account usage is physical-device-only, so the usage pages show
unavailable values and never draw a fabricated countdown, allowance ring or
history. Firmware version, build date, battery readings and dictation have no
source in this preview.

Cody Studio uses the 466 × 466 display at integer pixel scales. Resizing affects
presentation only. Rendering and touch share the same logical rotation.
Per Monitor v2 is enabled before Tk. Moving between monitors with different DPI
still requires a dedicated Tk transition.

The separate Luna desktop simulator and its packaging workflow use Windows.
Explicit Cody artwork and material views, such as `-Screen greeting`, retain the
legacy **Face**, **Shape**, **Inputs** and **Inspect** panels. Each tab retains
its own scroll position. In narrow windows the inspector moves below the preview
when vertical space permits. The artwork editor requests a 2x canvas when there
is room and supports its own timing controls and host FPS display.

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

`-Ccp` opens a real UDP endpoint restricted to `127.0.0.1:42117`.
Select **Connect Codex** on the simulated display and read the code.
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

The simulated identity starts with `02`. Windows keeps DPAPI-protected
associations in `artifacts/cody-simulator-state/device.json`. Linux stores its own
identity and references to the user's Secret Service in
`artifacts/cody-simulator-state-linux/device.json`. Pairing on Linux requires
`secret-tool` from `libsecret-tools` and an unlocked keyring. Build cleanup
preserves both directories. A second endpoint cannot take the same
port; a read error remains an error instead of inventing a new device.
The code appears only in the UI, never in logs or the public snapshot.

The device-facing **Connections** screen supports replacing a retained link,
cancelling the current code or candidate, forgetting simulator associations
after confirmation, and switching simulated Wi-Fi off and back on. Context
uses fresh authenticated activity. Chat selection follows the actual Codex
prompt.

CCP can explicitly request a restart or factory reset of this **SIMULATED** target,
using the distinct phrases and code described in the
[plugin README](../../plugins/cody-codex-plugin/README.md#explicit-restart-or-settings-reset).
A restart preserves the store. **Options > Factory reset** in Cody Studio also
offers an explicit on-screen confirmation. A factory reset clears simulator
settings and associations and leaves simulated Wi-Fi unconfigured.
To resume, enable **Wi-Fi** in **Connections** and pair again. The old PC
record remains separate.

`-Ccp -ActivityPreview` opens the legacy artwork inspector and its **CCP** panel.
The panel's explicitly synthetic activity, model prices, character and lower
technical panels are artwork study inputs. They stay separate from the active
endpoint and physical firmware.

CCP 0.6 also supports authentic EVENT messages. A question follows 26 → 27 → 31:
open it, select a card, review its description and press **Send**.
The selection stays local and reversible until Send; Back preserves the path.
A UDP ACK alone leaves **Confirming delivery**: **Reply sent** requires the second
recipient-confirmation poll within the 180-second TTL. A lost loopback link keeps the same question visible, with sending disabled
until a new authenticated snapshot arrives.

PR views display data only. **View failures** opens up to six complete names,
three per page, with vertical scrolling and Back navigation. Rows are not buttons;
merge, retry, logs and stack traces are not offered. New notifications replace old
ones without interrupting a question. Sending tools require
`include_simulator: true` even after pairing; **SIMULATED** stays visible, without
presenting the preview as firmware.

This mode exercises the protocol path and its host presentation.
The device preview models pairing and navigation without running ESP-IDF.
`-Ccp` is interactive and uses the default screen. Physical Cody remains an
independent identity.

## Expressions, symbols and materials

The Face panel selects a manga symbol for the `rig` presentation.
The semantic `sleep` and `sleeping` presets activate Sleep; closing the eyes with
a slider does not mean falling asleep. A manual face change removes the preset's
identity. Greeting, boot and Micro do not display the symbol.
To explore automatic petting, disable the diagnostic rig and select None.

The ordinary rig also uses the firmware character director for fresh pointer gaze
and a gentle warmth tail after a recognized caress. Explicit face presets, the
diagnostic rig, boot/setup/question routes and material studies retain their own
poses. These contributions share the native mood owner and preserve the response's
body motion, protective eye closure and audio mouth; pause freezes artistic time.

In the existing **SIMULATED PREVIEW** controls, select **Long compaction**, let its
face appear, then select **Refreshed / pending** to preview concentration followed
by release. Opening the completion scenario alone does not imply an observed start.
The authenticated simulator endpoint uses its admitted chat/sequence and source
freshness instead. Pointer gaze and visible activity text use logical coordinates
at the selected rotation; synthetic preview facts never enter that endpoint.
Physical behavior and full CCP panel interaction remain pending in
[TODO.md](../../TODO.md).

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
The complete contract is in [personality icons](../../docs/cody/personality-icons.md).

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
silently install dependencies. The first import of an external dependency
requires one targeted Windows Defender scan of the archive and extracted files.

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
documented in [HTTP device API](../../docs/bsp/services/http-device-api.md).

Prepare these checks through the console, then request the authorization required
by [AGENTS.md](../../AGENTS.md) before each execution:

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
no LAN broadcast or access to personal associations.
The character suite runs the real C++ direction binding, saves labelled synthetic
frames and exercises a withdrawn Tk window with its scheduled renderer. Prepare
both `cody-simulator-graphics` and `cody-simulator-audio` through the console first.
It checks gaze, accepted caress warmth, matching compaction completion, source
gaps and Debug isolation; it does not connect to a physical Cody or live Codex.
The raster suite compares pixels with the per-pixel compositor and an independent
coverage oracle on small masks. The Luna suite covers fixtures, actions, redaction
and loopback transport. Current acceptance gates are in [TODO.md](../../TODO.md).
For interpreters, paths and packaging, see the [console](../../scripts/README.md);
for the device, see the [runtime](../../docs/cody/runtime.md) and
[rendering](../../docs/cody/rendering.md) contracts.

`cody-runtime` includes regression definitions for the shared C++ modules;
`simulator` includes aging, RTC, publication errors and native JSON. The presence
of these definitions does not establish that they were executed. Suites loading
the library require an up-to-date driver before they start.

## Host audio and inspection ownership

The interactive preview uses a host-only output owner. The public
`device.ps1 simulator` route compiles `cody_fake_audio.dll` on Windows or
`cody_fake_audio.so` on Linux. The explicit generator is
`device.ps1 generate -Generator cody-simulator-audio`. Linux driver build
directories end in `-linux` to separate their outputs from Windows.

The library links the unchanged Cody personality selector, whistle and typewriter
modules to fake `LUNASHELL_audio_*` output functions. One bounded worker plays
their PCM with Windows `winsound` or streams WAV to ALSA `aplay` on Linux. Presented symbols, the visible whistle generation,
new greeting glyphs and pointer presses request sounds automatically. Boot
artwork routes and manual previews use the existing generated boot WAVs.
The PC sound toggle cancels pending clips; optional admission/playback failures
remain visible in the simulator status and host log without stopping the UI.
This is one output voice, with no mixer. PC playback is labelled synthetic and
never published into firmware snapshots. Capture-only routes do not initialize
the audio owner or play sound. Driver compilation does not establish audible output;
interactive playback remains a separate authorized observation.

Cody Studio uses Consolas for host controls and inspection labels. The four
inspector tabs share a seven-character label width and identical padding in
selected/unselected states; the Clam selected-tab padding/expansion defaults
are explicitly overridden. Character scene typography retains its authored
device assets. Palette text provenance is normalized both by the generator
and the Windows painter; indexed coverage and PCM binary hashes remain exact.

The Cody Studio artwork editor and Luna simulator desktop headers show the approved VibeWare
floppy at the top right, outside the simulated device canvas. The clickable,
keyboard-accessible badge opens `https://umbertogiacobbi.biz/vibeware/manifesto`
only on user activation. It indicates participation in the initiative, not
application authorship. Those windows use the Luna Shell project artwork for
their window icon, separately from the initiative badge.

The unchanged floppy source, original MIT notice and provenance are under
`simulator/windows/assets/branding/`. Cody Studio also retains its project and
personal-site links. Browser launch failures are retained and reported to the
host console. These source changes do not establish an interactive UI check.
