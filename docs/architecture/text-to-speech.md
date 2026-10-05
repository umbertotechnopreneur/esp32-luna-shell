# Optional local text-to-speech

Status: **implemented in source, 2026-10-05; validation pending**. Worktree:
`E:/ESP32 Luna Speaks`, branch `feat/luna-speak`, based on `main` at `1fbc0d7b`.
No TTS generator, tests/scans, configure/build/size, simulator, serial, flash,
SD provisioning or listening test has run. Compatibility, linked size, real-time
performance and physical sound remain unverified.

## Composition and languages

`CONFIG_LUNASHELL_BUILD_ENABLE_TTS` defaults to off. The native component and
its generator are admitted only by an explicit catalog feature. Ordinary Luna
and Cody profiles do not include the speech component, worker, buffers or data.
An incompatible manually edited sdkconfig fails the configuration/script gate.

| Selection | Data storage | Languages | Principal data bytes |
| --- | --- | --- | --- |
| None, default | None | None | 0 speech payload by source composition |
| `tts-sd` | microSD, loaded into PSRAM | English, Italian, Mandarin, Cantonese | 3,111,550 |
| `tts-flash` | Optional app component's read-only data | English only | 827,224 |

Counts come from the pinned package manifest; they exclude four/single voice-definition
files, alignment, engine code and runtime allocations. No linked firmware size
or disabled-build measurement is established. Mandarin (`cmn`) and Cantonese
(`yue`) are distinct identities; Italian is `it`, English `en`. Flash rejects
non-English requests with `ESP_ERR_NOT_SUPPORTED` and cannot embed their
individual dictionaries or voice definitions.

Both variants execute formant synthesis on the ESP32-S3; this is not a neural
voice model. The shared phoneme tables remain common to English too. A slow,
lower-pitched voice can be tuned later, but a HAL-like result is not established.
The board has microSD, 16 MB flash and 8 MB PSRAM in the reviewed
[hardware inventory](../../hardware-inventory.md). No SSD interface exists in
this integration. SD supplies storage, not execution resources or extra RAM.

Both Cody and full Luna have isolated SD/flash variants, generated sdkconfig,
build directory and dependency lock. Physical hardware selection remains
separate from UI selection: adding speech to Cody does not admit LVGL/Brookesia.
The supported target is ESP-IDF 6.0.2 with the existing Brookesia 0.8 UI path.
Wake-word/AFE plus TTS is rejected as an unreviewed combined composition.
No speech partition or LittleFS speech image is created. English flash data is
part of the optional application image; SD builds embed only file metadata and
hashes, while disabled builds admit neither speech code nor package generation.

## Native service and ownership

[Public API](../../components/LUNASHELL_tts/include/LUNASHELL_tts.hpp):
`initialize_speech()`, `request_speech(request, request_id)`,
`cancel_speech(request_id)`. Main startup creates the optional owner silently;
engine initialization and package loading are lazy until the first request.
No scene, UI, HTTP endpoint or connected assistant calls `request_speech`.
Deciding when Luna speaks is deliberately still open.

A single worker serializes the non-reentrant engine. One request may be queued
or active; another request is rejected. IDs are nonzero and never reused during
a firmware lifetime. Admission copies at most 1,024 bytes of validated UTF-8,
rejecting embedded NUL, control characters, malformed/overlong sequences,
surrogates and invalid code points. The caller retains its input until admission
returns; engine PCM pointers are borrowed only during the synchronous callback.
Cody UI text remains English ASCII.

Requests select a typed language and bounded voice parameters: 80–450 words per
minute, pitch/range 0–100; defaults are 145/40/20. Input enables UTF-8 only;
SSML and phoneme command flags are not enabled. Voice selection uses exact
package paths, without voice enumeration or an environment/filesystem fallback.

`DeviceSnapshotService` publishes the actual speech state, request/language,
storage, source PCM count/rate, timestamps, original engine status, filesystem
errno and operation error. Canonical projections supply aged clock, power and
storage observations; TTS never reads sensors/BSP directly. Unknown clock,
quiet hours or non-active power suppress requests. During synthesis, each PCM
callback checks current policy and cancellation again. Canonical publication
failure follows the platform fatal invariant; policy-read failures stop the
current request. Queue notification failure remains latched for later admissions.

The existing audio owner holds its codec mutex across the entire stream. It
preserves the current microphone/output clock, volume and previous mute state.
A rational sample-and-hold conversion with persistent phase adapts 22,050 Hz
engine PCM to that clock in bounded chunks. This inexpensive conversion has not
been evaluated acoustically. There is no mute/settle delay between engine blocks
and no second BSP/audio owner or unbounded PCM queue. Successful codec writes
publish actual playback activity. A speech cue has its own stable identity,
independent of personality sound IDs and speech request IDs.

At finish, the audio owner writes silence exceeding the BSP's configured DMA
ring capacity before restoring mute; cancellation/error mutes before retirement.
Failed drain/restoration poisons the route and retains the original failure.
`Completed` follows engine completion, transport retirement and successful state
publication; it does not prove that the physical speaker emitted the right sound.
Cancellation is cooperative, with latency from synthesis blocks or an in-flight
watched SD operation; instantaneous hardware interruption is not claimed.

## Data package and memory

[Versioned input](../../assets/tts/espeak-ng/manifest.json) contains matched
phondata/phontab/phonindex/intonations, four dictionaries and four definitions.
Each entry records exact size, SHA-256 and language. The generator rejects an
unexpected revision/path/language assignment and verifies selected source hashes.
Flash generates arrays for common/English files only; SD generates pointer/hash
metadata and an artifact-only card package. Build-local outputs are under the
selected `artifacts/` build tree, never source assets or root build directories.

The removable-storage owner is the only task that opens card files, always
read-only below `LUNASHELL/tts/espeak-ng-data`. Its watched request verifies all
twelve exact paths, sizes and SHA-256 using IDF 6's PSA crypto API. A stack-owned
request and its destination buffers remain alive until owner completion; no
caller timeout releases borrowed memory while I/O is still active. Standby,
performance suspension, missing/unverified card and owner failures are rejected.
It never mounts/formats another medium or falls back to flash.

The first successful SD request loads the immutable pack into separately
allocated PSRAM buffers. Later requests hash the current card's package again
through a small owner scratch buffer, without overwriting borrowed engine data.
A removed/replaced/altered package cannot be silently replaced by cached data.
Card disappearance during synthesis stops when its canonical owner publishes
that observation; polling/physical response latency remains to be measured.

Flash tables/dictionaries are borrowed from the application image. The native
allocator distinguishes exact registry pointers from owned allocations, so
borrowed arrays are never freed. Engine allocations use PSRAM exclusively,
including allocator headers in the configured budget; no internal DMA-memory
fallback is attempted. Default engine budget is 1 MiB and worker stack 64 KiB in
PSRAM, independently configurable within reviewed Kconfig ranges. These are
limits/reservations, not measured high-water marks. SD also needs about 3.1 MB
for immutable package data; existing UI/radio/runtime allocations still apply.
Libc registry-stream bookkeeping and task control use their native allocators.

An allocation failure escapes only the serialized C engine boundary; no C++
object lifetime crosses that escape. A partial/damaged engine is retained and
refused until firmware restart. Initialization failures are also terminal for
that instance. Absent/bad SD data before engine initialization can be retried by
a later explicit request. The snapshot keeps real failures throughout.

## Console usage, after authorization

These are examples for a future explicitly authorized operation, not commands
executed by this implementation task. Start in the new worktree. Use the same
profile/feature/build mode for configure, build, size and any later flash:

```powershell
.\scripts\device.ps1 set-target -Profile cody-companion -Feature tts-sd
.\scripts\device.ps1 build -Profile cody-companion -Feature tts-sd -Jobs 8
.\scripts\device.ps1 size -Profile cody-companion -Feature tts-sd
```

`-Feature tts-flash` selects English only; `-Profile full` selects the existing
Luna UI composition. `-Feature none` keeps speech excluded. The interactive menu
exposes the same optional feature setting and displays the active selection.
`-BoardConfig` remains an independent profile selector and cannot be combined
with `-Profile`/`-Feature`; these new variants are selected through the catalog.

Explicit package generation, without a firmware build or physical-card write:

```powershell
.\scripts\device.ps1 generate -Generator tts-assets -ToolArguments @('--backend', 'sd')
```

The card-ready artifact is
`artifacts/tts/sd/sd-root/LUNASHELL/tts/espeak-ng-data`. An authorized SD build
also produces it under
`artifacts/build-<profile>-tts-sd-<mode>/esp-idf/LUNASHELL_tts/tts-sd/sd-root`.
Copying it to a physical SD card is a separate operation; none was performed.
A firmware flash never provisions the SD card or selects a fallback data source.
All build/flash commands retain the existing transient-artifact cleanup and
configuration/dependency-lock checks from `scripts/device.ps1`.

## Provenance and outstanding evidence

Imported sources and matching data are pinned to
[pschatzmann/arduino-espeak-ng commit 93b2651](https://github.com/pschatzmann/arduino-espeak-ng/tree/93b2651d324874e1e00077af008d8ab83ab32e73),
eSpeak NG 1.52-dev. Only explicit native synthesis/Unicode C sources are compiled;
Arduino classes, AudioTools, POSIX emulation, desktop audio, pthread workers and
runtime dictionary compilers are excluded. Native modifications are recorded in
[ORIGIN.md](../../third_party/espeak-ng/ORIGIN.md). Original license notices and
GPL/Apache/BSD/UCD license files accompany the sources. Optional linked firmware
must satisfy the imported source licenses when distributed; the repository's
own license does not replace them. No external release has been performed.

The [upstream language list](https://github.com/espeak-ng/espeak-ng/blob/master/docs/languages.md)
and pinned definitions establish source support for the four chosen languages;
pronunciation quality on this device is unverified. The disposable reference
checkout under `artifacts/research/` is not required by the integration.

[TODO.md](../../TODO.md) retains all final validation and trigger decisions.
Fresh exact-command authorization is required before each test/suite and before
configure/build/size, simulator or physical operations. Required evidence covers
normal/SD/flash linked and flash-list exclusion, source/package contracts,
PSRAM/stack/CPU budget, continuous PCM, all four languages, cancellation,
standby/policy, card/package errors and actual listening. No successful host
assertion or build should be presented as proof of device audio behavior.
