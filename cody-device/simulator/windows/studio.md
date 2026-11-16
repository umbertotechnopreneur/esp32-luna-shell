[Desktop simulators](README.md) / Controlled Studio

# Controlled Cody Studio

The controlled Studio runner opens catalog destinations directly with isolated
synthetic inputs. The phase C pilot contains S01 Branding, S02 Birth, S17 Guide:
Buttons, P01 Settings, P04 Display, P12 Quiet hours editor, W02 Reset countdown,
W04 Codex limits, S15 Provisioning error and C04 Tool calls. Nine cases use the
full native runtime; C04 uses the production C++ browser presenter with a typed,
synthetic query owner. It does not establish domain transport/reducer acceptance.
Current validation evidence belongs in [done.md](../../../done.md);
remaining acceptance gates belong in [TODO.md](../../../TODO.md).
The approved Studio mockup defines
the host hierarchy and expanders; it does not replace device captions, icons,
settings or renderer geometry.

Public commands (run each only with the approval required by `AGENTS.md`):

```powershell
pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio
pwsh -NoProfile -File ./scripts/device.ps1 generate -Generator cody-simulator-graphics -ToolArguments '--device-runtime'
pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio -ToolArguments '--native'
pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio -ToolArguments '--pilot'
pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio -ToolArguments '--phase-e'
pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio -ToolArguments '--host-audio'
pwsh -NoProfile -Command '& ./scripts/device.ps1 check -Check cody-studio -ToolArguments @("--coverage","--statistics","--audio-cues")'
pwsh -NoProfile -File ./scripts/device.ps1 simulator -StudioCase P04
pwsh -NoProfile -File ./scripts/device.ps1 simulator -StudioCase S15
```

On a headless Linux host, exercise the real Tk/native window with retained
screenshots and isolated settings/preferences:

```bash
xvfb-run -a -s '-screen 0 1920x1200x24' pwsh -NoProfile -File ./scripts/device.ps1 check -Check cody-studio -ToolArguments '--ui'
xvfb-run -a -s '-screen 0 1920x1200x24' pwsh -NoProfile -Command '& ./scripts/device.ps1 check -Check cody-studio -ToolArguments @("--ui","--atlas-ui","--audio-cues-ui","--statistics-ui")'
```

This suite checks direct preparation of all ten cases, editing at input bounds
and replay back to the same pixels, scenario save/load, baseline preservation,
capture history synchronization, notes, keyboard focus, expander
restoration, visible status/Atlas controls and proportional layout at 1440x960
and 720x540. It checks evidence filters, saved baseline restoration and missing
baseline recovery, plus explicit compatibility and per-capture reviews.
Collapsed lower sections share a compact row on small windows.
Screenshots are host UI evidence and never substitute for device photographs.

`-StudioCase`, `-StudioScenario` and `-StudioCapture` explicitly select the
native controlled runner and reject legacy preview flags, including `-Screen`,
`-AtMs` and `-OutputPath`. This route does not implicitly rebuild a stale
runtime. Rebuilt libraries require a new process; provenance is checked again
before preparation and capture. The ordinary live window's Simulator menu also
opens a separate controlled Studio session using the same internal entry point.

The window provides catalog search/availability filters, synthetic battery,
network, civil clock, settings and case-specific page/service inputs, scenario
load/save, a proportional
native frame, explicit baseline preservation, side-by-side/overlay/pixel
difference and capture-specific human reviews. Changed and Needs review filters
cover captures loaded in the current session; availability filters include the
declared profile-conditional and not-applicable states. The chosen preserved
baseline is restored on reopening; if it is unavailable, its reference is kept
until an explicit replacement or clearing and the error remains visible.
Expanders persist independently of native state; Catalog and Inspector can
collapse on smaller windows. If `artifacts/simulator/studio/ui-state.json` is
unreadable or invalid, Studio uses temporary host preferences and shows a
persistent warning with the original error. Automatic saves, baseline selection
and closing the window cannot replace that file. A file that becomes unreadable
after startup also blocks saving. Only a genuinely absent file enables first-use
defaults; missing optional fields in a valid file keep their normal defaults.

To recover, close Studio, explicitly repair or move the preference file, then
reopen Studio. Repairing or removing it while the window is open does not unlock
that session. No automatic reset, deletion or repair is performed. Saved capture
and baseline files are separate and remain intact. Failed writes retain their
original error and may leave a unique `.tmp` artifact for manual inspection.

Source actions resolve the current definition in
the checkout and report missing or ambiguous matches. Routes show declared
origins, destinations, events, conditions and source evidence; opening a
destination and replaying a route are distinct actions. The native
coverage check exercises 42 declared touch/swipe contracts: 20 Settings and
confirmation-return routes, nine About routes, ten adjacent guide transitions
S17–S22 and three Bluetooth/saved-PC returns. These describe the declared model, not a complete navigation extraction.
S19 requires the compiled wake-word profile. QR pages first receive a central
tap to reveal controls and retire that intermediate frame, before the guide
arrow or About swipe. The destination frame and route receipt retire together;
other declared paths remain source analysis.

## Animation and audio

Animation provides play, pause, step, speed and keyframe seek for S01 Branding
and S02 Birth. Source sampling uses frame boundaries from the compiled native
renderer; the display-worker mode also runs the real startup animation worker
in an isolated process. Backward seeks restart that isolated scenario. Worker
captures retain both the requested instant and the actual completed-frame time;
they do not claim deterministic replay of all services, network, audio or timers.
Playback speed is limited by preparation throughput.

**Boot replay** opens a separate live runtime for S01 or S02, with a progressing
monotonic clock and a dedicated simulated settings store. It uses the firmware
audio owner, source PCM clips, Sounds and volume policy, and the native host
ALSA/WinMM backend. The host voice input is explicitly unavailable and stopped.
This is a returning-device replay, not an actual first-boot sequence. The UI
shows each cue outcome; backend initialization and a completed cue do not prove
that sound was audible through the PC speakers. Pause stops the live process;
restarting begins again. Its live frames are previews, not captures or baselines.
To listen, open S01 or S02, expand Animation, enable Sounds and choose a volume
in Scenario, then press Boot replay. Turn Sounds off before Apply and capture
or before using the capture timeline.
Static and isolated captures do not open the host audio device or emit sound.
**Audition cue** selects one of 26 authored firmware sounds (19 Codex and seven
Pet cues), using the current unsaved Sounds, volume and civil-clock/quiet-hours
inputs. It starts an independent native process with its own settings owner and
a naturally advancing audio clock. Its display remains parked so no unrelated
boot or navigation cue enters the audition. Stop cue and closing Studio reap
that child. Actual admission, suppression, completion and original errors are
shown as text. Audition does not create a capture, synthesize an event or prove
that a route triggered its sound; the interactive frame, NVS, pending form,
baseline and review remain unchanged. To hear a cue, enable Sounds, select the
volume and press Audition cue without applying the static capture form.
The normal firmware output mixer uses 22050 Hz; authored clips keep their source
formats and pass through the same native conversion as device playback.
Up to 40 recent thumbnails are displayed; capture artifacts remain available.
All device pixels come from native C/C++.

## Supported destinations and limits

The extended native contracts cover Settings pages and children, quiet-hours
summary/editor, confirmation pages, first-run/connection choices, five About
pages, diagnostics, the Device counter and all six Codex browser destinations.
Only confirmation views are prepared; no restart or reset is executed. Browser
pages use the production presenter and explicitly synthetic typed query owner;
Pending questions currently renders its genuine unsupported-source/events
handoff, not invented question content or reply acceptance. Remaining pages
retain specific owner/dependency reasons. Retired, retained and planned entries
are classified separately as not applicable. Source-catalog counts include
families and historical identities, not only unique active device screens.
Offline recovery is deferred because its current native presenter overlaps the
character and title. Project QR and guide captions retain their authored initial
viewport and any source layout limitations; acquisition alone is not approval.

Statistics now includes Today, Last seven days, Token history, Credits, Account
and Context with explicit typed source observations. Validity masks distinguish
missing buckets from measured zero, and source age follows the native lease and
aging rules. Eight daily samples and 96 chronological quarter-hour samples are
editable as JSON integer arrays in Scenario. Array capacities, integer ranges
and printable captions are validated before the native request; no truncation
or padding repairs an invalid input. Voucher expiry dates remain unavailable.

Wi-Fi and pairing views use fixed synthetic scan, credential-entry, progress,
QR and saved-PC fixtures. Opening them starts no radio or credential operation.
Their declared input and route limits remain attached to each capture. Audio
uses the actual host-profile availability and errors; unavailable microphones
or playback observations are not replaced with PC measurements. Memory uses
the original isolated C++ diagnostic presenter, with unavailable heap data and
a graphical Back control whose input/navigation is not replayed.

An inventory family can refer to several distinct source states. Its primary
ID remains an unambiguous direct-entry alias; secondary members retain their
qualified source key and show the same document ID with `(family)`. For example,
`S08` opens Networks, while `ui-v1:wifi-scan-unavailable` opens its unavailable
scan member. Both keep their distinct capture/scenario identities and the
explicit family link in the atlas and PDF.

## Generate and export an atlas

The Atlas expander runs the pilot, all supported contracts or the currently
filtered catalog in a separate process with a separate native library and NVS.
Generate atlas reports progress and each captured, failed, unsupported or
not-applicable case. Cancel stops that child and retains the last published
atlas. Neither generation nor publication prepares the interactive scenario,
selects a different current frame, writes its settings or changes the chosen
baseline. Published captures appear as thumbnails; click one explicitly to
inspect or compare it. Each generation reruns its entire selection.

Automatic regeneration is opt-in for each window. It watches only the native
build manifest and library, validates the complete source/binary provenance,
and starts after a newly validated build. Atlas output cannot retrigger it.
An outdated build is rejected rather than rebuilt implicitly. Failed,
cancelled or stale jobs leave the previous valid publication intact. Historical
results keep their build identity and do not establish current-source coverage.

Export PDF uses the same validated, immutable atlas manifest as the window.
It includes the original inventory document revision and mapped IDs, stable
source identities, variants, rendering modes, source functions and reasons for
absent captures. The selected-suite count and complete source-catalog count
remain separate; neither is an assertion of physical acceptance. Exports and
intermediate artifacts stay under `artifacts/`. The [native atlas standard](../../../docs/cody/native-atlas-standard.md) specifies the three-screen layout, evidence and on-request annotation films. The documented Obsidian mirror is a separate delivery step.

```powershell
pwsh -NoProfile -Command '& ./scripts/device.ps1 generate -Generator cody-studio-atlas -ToolArguments @("--suite","supported")'
pwsh -NoProfile -Command '& ./scripts/device.ps1 generate -Generator cody-studio-atlas -ToolArguments @("--case","P04","--case","S15")'
pwsh -NoProfile -File ./scripts/device.ps1 generate -Generator cody-native-screen-atlas -ToolArguments '--render'
pwsh -NoProfile -Command '& ./scripts/device.ps1 check -Check cody-studio -ToolArguments @("--atlas","--atlas-pdf")'
```

The PDF generator defaults to the published atlas; `--manifest` selects a
retained result and `--output` selects a new PDF beneath `artifacts/`.
`--render` additionally produces retained PNG previews of every page for visual
review, using Poppler. Each export and preview prefix is unique.
PDF export loads ReportLab only when requested; ordinary Studio capture does
not require it. The PDF verification suite additionally uses pypdf and
Poppler to inspect extracted content, native image bytes and rendered pages.
Select an existing interpreter containing these packages with
`-PythonExecutable <python>` when the default host interpreter lacks them.
The phase-F Tk check runs in its own native process, so `--ui` and `--atlas-ui`
can be combined without attempting to reinitialize process-global audio owners.
Use `--suite all` to retain explicit absences for every source-catalog entry.
The suite exposes only declared native contracts, not an inferred complete
navigation graph. Windows and physical Cody remain separate acceptance gates.

## Scenarios, baselines and reviews

Saving scenarios and reviews writes under `artifacts/simulator/studio/`.
Scenario saves refuse replacement; captures and baseline copies use unique
directories and never overwrite existing evidence. A comparison requires the
same target, canonical scenario revision, variant, geometry, rendering mode,
fixed native dependencies, capture instant, worker-requested instant and navigation provenance. The full
route chain is recorded from the last direct preparation; a direct capture and
a route capture do not imply the same animation state. Historical route captures
without that chain remain readable but are not comparable. Acquisition, pixel difference and human review are independent;
changed pixels do not declare an error. Previously saved baselines are accepted
with their historical build hashes and source identity, including after a source
file is moved. Their scenario must still satisfy the current catalog schema;
historical schema versioning and independent validation of retired route contracts remain open.

## Replay and time contracts

Replay a saved scenario or capture without a window using the same runner:

```powershell
pwsh -NoProfile -File ./scripts/device.ps1 simulator -StudioScenario artifacts/simulator/studio/my-case.json -StudioCapture
pwsh -NoProfile -File ./scripts/device.ps1 simulator -StudioCase P04 -StudioCapture
```

The default time contract is a **settled static sample at t=0**: monotonic time
freezes before bootstrap and civil time comes from the scenario. S01 and S02
also support source-animation samples and an isolated display-worker replay;
neither establishes a controlled schedule for unrelated worker threads. The
display owner prepares each destination and its caller/return context before rendering.
Only orientation 0/180 is supported by this host profile. The capture is the
logical raster before hardware panel rotation; auto rotation has no simulated
gravity observation. No arbitrary delay or post-capture route query assigns
identity. General worker replay remains deferred under `CODY-NEXT-06`. The inventory is **Cody
screen map and static inventory**, 14 November 2026, baseline `e971b313` plus
local changes; its SHA-256 and repository-relative artifact location are in
`studio_catalog.json`. Its inventory section contains 100 document-local IDs,
verified separately from the wider source catalog and native acquisition coverage.
Declared links describe the document model. No physical acceptance
is claimed.

## Verification scope

The default suite checks contracts without loading the native library. Its
`--native` extension runs two fresh processes/settings stores and compares
exact RGB pixels plus same-frame identity; it does not establish device parity.
The `--pilot` extension captures every pilot case twice in fresh processes,
compares exact pixels/receipts, and exercises forward/reverse case order plus
error/recovery and changed C04 scopes in a shared native session. It rejects
invalid inputs and stale build provenance, preserves baseline bytes and writes
a labelled contact sheet only when every case succeeds. Declared routes are
not thereby reproduced. C04's ordinary user-facing entry remains unconfirmed;
S15 scripts a transient provisioning failure without sending a real request.
The `--audio-cues` extension checks all 26 authored cues through native PCM,
including Sounds, quiet hours, volume and cancellation. `--audio-cues-ui`
exercises the real Tk audition controls in a fresh native process and verifies
that the interactive frame, settings, pending form and baseline are retained.
The `--statistics` extension checks visible data edits on all six statistics
pages, prior-case retirement and invalid native request rejection with an
unchanged completed frame. `--statistics-ui` exercises JSON array editing and
family identity in the real Tk/native window.
The `--phase-e` extension checks source-frame samples, controlled display-worker
replay, owner-dispatched settings and guide routes, atomic receipts, invalid
inputs, logical orientation, a pending external contact and live firmware PCM
with Sounds on and off. It does not claim that every concurrent touch
interleaving or every asynchronous worker was observed. `--host-audio` sends
the two intro cues and one authored Pet tap cue to the selected PC output and
may make audible sound.
Generated artifacts are retained. Cleanup remains manual and requires an
explicit request.
