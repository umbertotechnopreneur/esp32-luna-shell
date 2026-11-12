<img src="https://raw.githubusercontent.com/umbertotechnopreneur/VibeWare/main/Branding/vibeware-logo.png" alt="VibeWare floppy logo" width="72">

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
.\scripts\device.ps1 simulator -Screen question-flow -AtMs 8000 -OutputPath artifacts/simulator/cody-questions.png
```

Cody Studio runs from source on Windows and Linux with PowerShell 7, a native C++20
compiler, Python with Tk and Pillow ImageTk, and PyYAML. The Apple store QR,
illustrative pairing QR and GitHub project QR also use the existing optional
ReportLab tooling; when it is absent the preview shows QR unavailable. Android
retains its authored QR.
On Ubuntu, the host packages include `g++`, `python3-tk`, `python3-pil.imagetk`
and `python3-yaml`. Use a graphical
session for the interactive window. Host audio uses ALSA `aplay` from `alsa-utils`.
The console builds the host audio driver before interactive launch. Static captures
use the graphics driver and do not start audio.

```bash
pwsh -NoProfile -File ./scripts/device.ps1 simulator
pwsh -NoProfile -File ./scripts/device.ps1 simulator -Ccp
pwsh -NoProfile -File ./scripts/device.ps1 simulator -Screen rig -OutputPath artifacts/simulator/cody.png
```

The default Cody Studio opens the device preview. The existing VibeWare floppy
plays once, followed by the native Muse showcase. **BOOT / Home** returns Home;
**BOOT 2s / Options** opens the device menu. **PWR / Standby** toggles the simulated display. With **Shake
standby/wake** enabled, **Simulate shake** toggles standby; otherwise it returns
to the first enabled statistics page when that dashboard is open.
Touching Muse Home reduces and lifts the original character to reveal **PAIR WITH
APP** and **SETUP WIFI**; they fade away after inactivity. The first opens the
Bluetooth introduction, while the second opens the Wi-Fi journey.
The journey offers app download, pairing and an on-device setup preview. The
on-device page shows a synthetic scan and fixed demonstration networks with
simulated signal strengths in dBm; selecting one opens a Studio recreation of
the native multitap password keyboard. Connect
changes only the optional simulated loopback state, and the password is neither
sent nor saved. The native keyboard is not exposed through the Studio bridge.
The app path includes an introduction with Android and Apple choices. Tap a
store QR to return to the introduction, then use Next to see the pairing QR.
The pairing QR shown in Studio is explicitly synthetic and
cannot pair a device. The device creates its own session-bound QR. A violet
no-Wi-Fi notice appears after the wake and four seconds of visible Home time;
its device name and link state are explicitly simulated in Studio. The shared
status bar appears across interactive screens, events and the screensaver; it
shows the PC's local date/time and unavailable physical measurements. It is hidden
during floppy boot and black standby. The status and navigation controls fade
after inactivity and return on touch or simulated motion. The numeric battery
setting is prepared in the renderer but has no settings control yet.

Dashboard navigation controls fade after ten seconds. A touch reveals them
again. **Tools** contains **Connections** and **Codex stats**;
**Preferences** contains **Display** and **Behavior**;
**Device** holds **Restart device**, **Factory reset**, **Quick guide** and an
**About** action. Settings menus use horizontal swipes to move between pages
or return to their parent; position marks show the active page. A short BOOT
press returns to Muse Home. The guide previews the three
fixed instruction pages and their lateral arrows. Restart replays the boot and
preserves settings and the simulator association. Brightness starts at 80%; brightness
and orientation choices affect this preview session. Menu labels remain on one
line and are ellipsized when they do not fit their row.

**About** contains **System**, **Memory** and **Firmware**, followed by the
VibeWare logo and a QR linking to the GitHub repository. A tap on the QR returns
to VibeWare; there is no Open link action. System identifies its uptime as
**Studio uptime**. The current loopback source has no physical CPU/RAM or
firmware metadata: those sections show unavailable states, without host
measurements or invented numeric bars.

The carousel can contain Device, Reset countdown, Context, Codex limits, Today,
Last 7 days, Token history, Credits, Codex account and About. Info and its QR
are child pages, not additional items in that cycle. The loopback endpoint supplies
authenticated link and Context observations; pages without supported, current
data are excluded from the carousel. The Device page separates
Codex, Wi-Fi and battery status. Firmware version, build date, battery readings
and dictation have no source in this preview. The device-first window does not
duplicate the firmware effects controller in Python or establish audible output
on the physical device.

Counter mode uses the same Context presentation, with the measured remaining
tokens, bar, context window and current compaction state when available. It
excludes Context when neither current capacity nor current compaction is
available, and excludes Model when the model observation is missing or stale.
Studio does not expose account allowance data, so it cannot display the reset
counter. When no Counter page has data, Studio shows Device instead. Page marks
and horizontal swipes include only the available pages.
For a fresh, recognized model identity, the Model page uses its retained Terra,
Sol, Luna or Astra master as a faint background when the file is available;
unsupported identities have none.

The third Behavior page includes Alarms and its Weekly usage switch. In Studio
this preference lasts only for the preview session. The loopback source does not
publish weekly account allowance or its reset time, so Studio cannot trigger a
weekly alarm, animation or sound; it never substitutes fabricated usage data.

**Behavior** followed by a left swipe exposes **Screensaver**, which enables the optional globe after
fifteen idle seconds on the dashboard. It is Off by default. This is a small
procedural preview at eight frames per second, not a firmware performance
measurement. A touch only dismisses it; shake returns Home. Options, pairing
and active Codex events remain visible. The preview's standalone window and
status still identify it as SIMULATED; the globe shares its display with the
adaptive status bar.

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

## Muse animation conformance

Studio's Animation selector exposes the 13 authored showcase combinations.
Preview Select starts the chosen pose paused at its first frame; Play, Pause and
Restart control its visible clock, and Showcase restores automatic playback.
The separate Physical Cody controls send the selected identity through the
authenticated CCP broker on the local network. Select, Play, Pause and Restart
control the device preview; Restore returns to its retained screen. Touch,
standby or five minutes without a command also end the device preview.

Physical playback requires a paired Cody running firmware with animation control
and a current CCP broker. The broker targets one fresh physical capability and
reports an error if multiple physical devices are available. A timeout has an
unknown outcome and is not retried automatically. The device confirms application;
the confirmation does not establish physical display appearance or performance.

The native preview uses the same showcase scheduler as the firmware. Each
simulated boot resets the scheduler and its artistic seed. The clock advances
only while Muse is displayed; standby, menus and event overlays pause it. A
touch reaction is retired when Muse is hidden. Visible time starts with Waking,
then Idle. At ten seconds the scheduler draws a pose
from its shuffle bag. It never repeats the previous pose, and an exhausted bag
starts another cycle. The bag contains the original modes plus authored manga
symbol variations. Sleep holds the visible closed-eye stage. This showcase
drives artwork only; it is not a real activity or sound signal. The SIMULATED
motion buttons provide synthetic directional impulses to the same motion filter
and artwork composition as firmware. They never read a real IMU. Turn 90° applies
an ideal cardinal device turn; with Auto-rotate On the preview keeps content
upright, and with it Off the manual panel setting is retained. This does not
simulate sensor noise, gyro drift, dwell or hardware timing. Its legacy
Options/CCP preview also does not yet expose the vNext native Codex
browser or the state-v1 multi-chat projection.

The **SIM Sophia** button is a synthetic wake-only shortcut for simulated
software standby. It does not listen through the PC microphone, perform speech
recognition, start dictation or establish physical wake-word behavior.

The native preview and firmware use the same C renderer and
[CODY_muse_frame](../../components/CODY_companion/include/CODY_muse_frame.hpp)
composition. The pose, active clock, elapsed pose time, accepted tap time and
optional audio energy are inputs. The frame has a 64 x 64 index grid and two
RGB565 palettes. The drawing adapter scales those indices into the caller's
logical viewport and preserves the output surface's RGB565 byte order.
The firmware retains the two indexed frames in its display owner until the
submitted DMA strips retire. The host writes into a caller-owned native RGB565
buffer synchronously, with no device access.

The following authored cases are for the vNext scenario driver and authorized
phase-11 execution. Use only synthetic pose and energy values for conformance.
Start at 0 ms and step by 40 ms. Pass elapsed time from the beginning of each
pose as pose time. Use an inactive reaction (1600 ms) and zero audio energy
unless the case specifies otherwise.

| Case | Input and checks |
| --- | --- |
| Seeded replay | Reset with seed 27. Use Waking until 1520 ms, then Idle until 5200 ms. Admit a tap at 1920 ms and pass its elapsed time. Replay the same sequence after reset and compare indexed frames and palettes on each target. Include the tap hold, fade and expiry. |
| Interrupted reply | Reset with seed 27. Use working Thinking from 0 to 640 ms, Reply at 640 ms with 0.7 energy, then Idle at 800 ms with zero energy. Replay with Reply energy zero throughout to represent absent audio. Check accepted poses, continuity at both interruptions and a caption-only reply without an audio acquisition request. |
| Viewport and strips | Replay the first case at 192 px on the current board layout, and at 320 px and 128 px on synthetic host surfaces. At each size, compare the full surface with the result of composing it from tiles of different heights and horizontal origins. At 128 px there is no shaded grid edge. Reject a 191 px viewport before writing pixels. |

The host ABI exposes `cody_host_muse_reset` and `cody_host_muse_pose_frame`
for explicit pose sequences. Compare exact output for replay on the same build.
`cody_host_muse_face_frame` and `NativeMusePreview.face_image` additionally
accept independently selected eye shapes, mouth shape and manga symbol alongside
body, hand and gaze offsets; explicit Talking openness is supplied by the caller.
The ordinary preview keeps automatic
expressions and uses the same native composition as the device.
Between targets, check the pose transitions, reaction timing and geometry
invariants. Floating-point libraries can affect individual indexed edges or
palette values. Hardware timing and audio remain physical acceptance gates.

## Connect the simulator to CCP

```powershell
.\scripts\device.ps1 simulator -Ccp
```

`-Ccp` opens a real UDP endpoint restricted to `127.0.0.1:42117`.
Select **Connect Codex** on the simulated display and read the code.
In Codex, explicitly request a connection to **Cody Studio SIMULATED**; discovery
adds `include_simulator: true`, then pairing uses the visible code.
For installation and trust, follow the [canonical CCP README](../../../cody-desktop/plugins/cody-codex-plugin/README.md).

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

The device-facing **Connections** screen offers **Connect Codex** and **Set up
Wi-Fi** as two side-by-side icons. The Codex flow supports replacing a retained
link and cancelling the current code or candidate. Use the host **Simulated link**
menu to toggle simulated Wi-Fi or forget simulator associations after
confirmation. Context uses fresh authenticated activity. Chat selection follows
the actual Codex prompt.

CCP can explicitly request a restart or factory reset of this **SIMULATED** target,
using the distinct phrases and code described in the
[plugin README](../../../cody-desktop/plugins/cody-codex-plugin/README.md#explicit-restart-or-settings-reset).
A restart preserves the store. **Options > Factory reset** in Cody Studio also
offers an explicit on-screen confirmation. A factory reset clears simulator
settings and associations and leaves simulated Wi-Fi unconfigured.
To resume, select **Toggle simulated Wi-Fi** from the host **Simulated link**
menu and pair again. The old PC record remains separate.

`-Ccp -ActivityPreview` opens the legacy artwork inspector and its **CCP** panel.
The panel's explicitly synthetic activity, model prices, character and lower
technical panels are artwork study inputs. They stay separate from the active
endpoint and physical firmware.

CCP also supports authenticated EVENT messages. A question follows 26 → 27 → 31:
open it, tap a numbered reply, review its full text and press **Send reply**.
The selection stays local and reversible until Send; Back preserves the path.
The capture-only `question-flow` screen shows the notice, pending queue, both
choice pages, both review pages, delivery states and the read-only PC approval
with synthetic data. It uses the device-first native renderer without pairing,
starting an endpoint or sending replies.
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
[TODO.md](../../../TODO.md).

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
The complete contract is in [personality icons](../../../docs/cody/personality-icons.md).

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
silently install dependencies. Verify the official source and version of a new
external dependency before use.

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

---

**VibeWare** · Created by Umberto Giacobbi · [Cody on GitHub](https://github.com/umbertotechnopreneur/esp32-luna-shell) · [VibeWare](https://github.com/umbertotechnopreneur/VibeWare)
