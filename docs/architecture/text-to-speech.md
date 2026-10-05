# Optional local text-to-speech

Status: **feasibility and integration design, 2026-10-05**. The native eSpeak
component, compile-time feature, asset pipeline and streaming service are not
implemented. This guide does not establish ESP-IDF compatibility, runtime memory
use, real-time synthesis or audible device behavior.

The study branch is `feat/luna-speak` in `E:/ESP32 Luna Speaks`, based on local
`main` at `1fbc0d7b`. Existing Luna and Cody builds must remain lightweight.

## Decision

A curated eSpeak NG port is a plausible offline candidate for ESP32-S3.
Keep the synthesis engine in firmware; select internal flash or microSD for its
phonetic and pronunciation data. eSpeak uses formant synthesis, not a neural
voice model. A slow, low-pitched voice is configurable, but HAL-like naturalness
has not been established by listening.

The requested languages are English, Italian and Chinese Mandarin. Mandarin is
the working interpretation of "Chinese"; Cantonese is a distinct available
upstream language, outside the selected package. The upstream
[language list](https://github.com/espeak-ng/espeak-ng/blob/master/docs/languages.md)
and the pinned port's definitions include `en`, `it` and `cmn`. Language support
in those sources does not prove pronunciation quality on Luna.

Prefer immutable, curated three-language data embedded in the optional
component's flash read-only section for the first integration. This keeps essential speech
independent of a removable card and avoids filesystem-to-RAM copies of the
large tables, once the loader's ownership rules have been corrected.

MicroSD is a separate explicit storage selection, useful for replaceable voice
packages. It does not add working RAM or execute the synthesis engine. The
documented board has microSD; an SSD interface/driver is not present in the
reviewed inventory or firmware. An external SSD would be additional work.

## Evidence and limits

Current local hardware/configuration references:

- [Hardware inventory](../../hardware-inventory.md): ESP32-S3R8, 16 MB flash,
  8 MB PSRAM, ES8311 speaker codec and user-added 8 GB microSD. These capacities
  are documented topology, not current free-memory measurements.
- [Board defaults](../../config/sdkconfig/v1/boards/waveshare-esp32s3-touch-amoled-1.75.defaults):
  octal PSRAM enabled, instruction fetch from PSRAM enabled, read-only data
  retained in flash. Added engine code can therefore also consume PSRAM.
- [Ordinary partitions](../../partitions.csv): 14,476 KiB factory app and
  1,024 KiB LittleFS. Partition capacity is not the available app headroom.
- [Wake-word partitions](../../config/partitions/wake-word.csv): separate
  512 KiB ESP-SR `model` partition. This contains recognition data and is not
  a TTS storage allocation.

The reviewed embedded port is
[pschatzmann/arduino-espeak-ng at 93b2651d324874e1e00077af008d8ab83ab32e73](https://github.com/pschatzmann/arduino-espeak-ng/tree/93b2651d324874e1e00077af008d8ab83ab32e73).
Its author reports an Arduino ESP32 minimum example of 1,457,411 program bytes
and 39,616 bytes of globals. These are external results, not the incremental
cost on Luna, total runtime heap use or proof for ESP-IDF 6.0.2. The original
eSpeak's approximate 2 MB claim is also not a Luna memory budget.

Static file metadata from that pinned port gives the following principal files
in `espeak-ng-data-min`:

| File | Bytes | Purpose |
| --- | ---: | --- |
| `phondata` | 554,740 | Phonetic synthesis data |
| `phontab` | 58,652 | Phoneme tables |
| `phonindex` | 43,316 | Phoneme indices |
| `intonations` | 2,312 | Intonation tables |
| `en_dict` | 168,204 | English pronunciation rules/dictionary |
| **Principal binary total** | **827,224** | **About 808 KiB, excluding voice metadata and engine** |

The minimal English language definition adds 148 bytes; that checkout also
contains two voice variants of 376 and 474 bytes. These files establish a
candidate payload size, not completeness of a newly curated package. Keep data
and engine revisions coupled; do not regenerate tables with an arbitrary host
version or treat missing phonemes as an acceptable partial success.

The user's subsequent Italian and Chinese requirements change the payload:

| Selected data | Bytes | Scope |
| --- | ---: | --- |
| Common principal tables | 659,020 | `phondata`, `phontab`, `phonindex`, `intonations` |
| English `en_dict` | 168,204 | British English `en`; an American variant is optional |
| Italian `it_dict` | 154,408 | `it` |
| Mandarin `cmn_dict` | 1,566,347 | `cmn` |
| **Three-language principal total** | **2,547,979** | **About 2.43 MiB / 2.55 MB before metadata and engine** |

These sizes come from the pinned checkout's full `espeak-ng-data` files.
Package the matching language definitions and all required phoneme dependencies
from that full data revision; do not simply add dictionaries to an unchecked
English-only package. Mandarin accounts for most of the additional storage.
The total cannot fit in the existing 1-MiB LittleFS even before GUI resources.
It could consume app flash or a separately selected partition, subject to the
actual enabled firmware size. No app headroom has been measured.

## Compile-time opt-in and script contract

The required primary symbol is `CONFIG_LUNASHELL_BUILD_ENABLE_TTS`, with
`default n`. This is a proposed symbol, not an available switch in this
checkpoint. No ordinary profile, full-services profile or board YAML should
implicitly enable it. Audio output being enabled must not select TTS.
Language choices must also govern packaged assets: proposed symbols
`CONFIG_LUNASHELL_TTS_LANGUAGE_ENGLISH`, `CONFIG_LUNASHELL_TTS_LANGUAGE_ITALIAN`
and `CONFIG_LUNASHELL_TTS_LANGUAGE_MANDARIN` exist only under enabled TTS.
The explicit TTS feature's reviewed defaults should select the requested three;
an excluded language must contribute neither its dictionary nor voice metadata.
Use a typed `SpeechLanguage` identity with stable English/Italian/Mandarin values;
do not infer a voice from an array index or ambiguous `zh` alias.

When TTS is disabled, exclude its component roots, source files, data headers,
generated assets, dependency resolution, worker, buffers and initialization
calls. Do not rely on a runtime toggle or linker garbage collection to make
the normal build small. Keep physical output capability separate from UI;
TTS must not admit LVGL or a Brookesia GUI adapter into native Cody builds.

Implementation must cover the entire selection path:

1. Add an explicit feature selection to [device.ps1](../../scripts/device.ps1)
   and [the firmware command](../../scripts/commands/firmware.ps1), with a
   corresponding variant in the [profile catalog](../../config/sdkconfig/v1/catalog.json).
   The current accepted features are `none`, `distance` and `wake-word`;
   `-Feature tts` is a future interface, not a runnable command today.
2. Admit the optional native component before IDF component discovery, following
   the catalog's roots/directories pattern. Add isolated defaults, dependency
   lock, SDK configuration and build directory for the enabled variant.
   A CMake conditional after discovery alone is insufficient.
3. Define explicit, mutually exclusive flash/SD data choices under the enabled
   symbol. Require an already selected compatible audio output; do not turn on
   audio hardware automatically. SD data additionally requires the selected SD
   service. Reject invalid combinations rather than selecting another backend.
4. Gate asset generation in CMake and the wrapper on the same resolved feature
   and storage choice. Generate build-local intermediates under `artifacts/`.
   Select only the reviewed enabled languages, not the port's all-language
   umbrella `src/espeak-ng-data.h`.
5. For embedded flash arrays, data becomes part of the enabled app image only;
   there is no separate TTS data image to flash. For SD, do not embed the package
   in the app or LittleFS, or add a TTS entry to the firmware flash arguments.
   Provision the card through an explicit owner-controlled operation.
6. If a dedicated flash partition is later chosen, give only that enabled
   variant its own partition CSV, generator target and flash entry. Ordinary
   profiles must retain their current partition table and flash list. Do not
   enlarge shared LittleFS or reuse the wake-word `model` partition silently.
7. Build, size, flash, combo/recover and generators must use the selected
   variant's artifacts. Reject SDK configuration/feature disagreement, stale
   asset paths or partition mismatch before writing. Normal selection must
   never pick a previous TTS variant just because its build directory exists.

An ordinary app flash does not guarantee physical erasure of data written by an
earlier experimental image. The requirement here is that disabled builds carry
and flash no TTS payload. Erasing old bytes is a separately authorized operation;
never add automatic erase or SD formatting to feature selection.

## Execution path

Proposed native path:

```text
Bounded validated UTF-8 request + explicit SpeechLanguage
  -> one TTS worker and one serialized eSpeak context
  -> text normalization / phonemes / formant synthesis
  -> borrowed PCM16 blocks from the synchronous callback
  -> bounded playback stream owned by LUNASHELL audio
  -> existing esp_codec_dev / I2S / ES8311 / speaker
```

Use `AUDIO_OUTPUT_SYNCHRONOUS` with `espeak_SetSynthCallback`; the owning
FreeRTOS worker calls synthesis and receives PCM blocks. This avoids the
desktop playback backend and a second asynchronous/threading owner. Copy a
borrowed block into a bounded queue if consumption outlives the callback;
never retain the engine's pointer after its callback returns.

The port's example produces mono signed PCM16 at 22,050 Hz, matching the
ordinary audio initialization in
[LUNASHELL_audio.cpp](../../components/LUNASHELL_platform/src/audio/LUNASHELL_audio.cpp).
Always use the rate returned by the initialized engine. TX and microphone RX
share the codec clock. With Sophia/AFE active, the existing voice path requires
16 kHz; TTS then needs a reviewed streaming resampler or an explicit microphone
handoff. Never relabel 22,050-Hz samples as 16 kHz or independently retime TX.
Combined TTS plus wake-word selection is separate integration work, not an
implicitly supported catalog combination.

The existing `LUNASHELL_audio_play_pcm16` function accepts PCM but applies
100-ms settling and 100-ms completion delays plus mute/unmute for every call.
Calling it for each short TTS callback would create gaps. Extend the audio
owner with a stream session that configures once, accepts bounded blocks,
drains, then restores the previous policy once. Preserve its shared mutex,
AEC reference feed, quiet/volume settings, standby, codec errors and canonical
playback publication. Do not create another BSP/I2S/speaker owner.

As a planning example, two 20-ms mono PCM16 buffers at 22,050 Hz contain 1,764
bytes of sample payload; this is arithmetic, not the full TTS memory budget.
Engine tables, translation state, phoneme queue, stack, allocator metadata and
audio transport add costs. No total heap, stack or CPU figure is available yet.
Synthesis must yield without starving display/radio/acquisition owners; affinity
and priority require measurements with the complete selected firmware.

## Flash and SD loading

For flash, expose a curated immutable asset registry to a native loader or
modify the engine loader to borrow validated flash tables. IDF maps linked
read-only data for CPU reads; a dedicated raw partition would instead need a
lifetime-owned mapping. LittleFS files are not direct pointers to flash tables.
The current 1-MiB LittleFS is shared GUI package storage: the approximately
2.43-MiB principal three-language payload exceeds its entire capacity.

For SD, extend the existing storage acquisition owner to verify and read a
complete, versioned package. Load required random-access tables into explicitly
budgeted PSRAM before synthesis. In the examined port, `ReadPhFile` and
`LoadDictionary` read whole files when the memory mapping path is unavailable.
SD storage therefore does not eliminate table copies. If all three principal
datasets are retained at once, they alone occupy about 2.43 MiB in PSRAM.
A serialized language-switch lifecycle may retain fewer dictionaries, but
must account for secondary-language dependencies and cannot assume that the
engine holds only one dictionary. The exact runtime total remains unknown.
Do not bypass the SD owner with consumer-side mounts/probes or permit card
access while it is quiesced.

Backend selection is fixed by the build. Missing card, missing/corrupt file,
wrong data version or failed allocation makes selected TTS unavailable, with
the real error retained. Do not switch to flash, synthesize with partial data,
substitute a default voice or format the card. Firmware can continue only when
TTS is explicitly documented as optional and its failure is published.

## Porting work that must precede use

The Arduino port is evidence and reference source, not an IDF dependency to
drop into the build. Its CMake project fetches an Arduino emulator and POSIX
filesystem library; its audio adapter uses Arduino AudioTools. A native IDF
component must have an explicit reviewed C-source list and native configuration,
allocation, data loading and PCM callback paths, with those dependencies absent.

Static source review identified concrete corrections required before integration:

- `ReadPhFile` can borrow mapped tables, but `FreePhData` unconditionally frees
  their pointers. Record owned versus borrowed storage and release only owned
  allocations; keep flash/mapping lifetimes through engine termination.
- The port's allocator attempts PSRAM then ordinary `malloc`. Select explicit
  budgets/capabilities for table and working allocations so TTS cannot consume
  internal DMA headroom silently when PSRAM is exhausted.
- Some working-buffer allocations are followed by `assert`, including in
  `phonemelist.c`, `translateword.c` and `voices.c`. Replace abort-on-allocation
  behavior with propagated failures and defined partial-initialization cleanup.
- File-loading error paths require exact short-read/corruption/error handling.
  Preserve engine/filesystem detail when mapping it to `esp_err_t`; an engine
  status is not automatically a valid ESP-IDF error code.
- Keep one engine context/worker; several synthesis tables are shared global
  state. Add bounded request IDs, cancellation, shutdown/drain and buffer
  lifetimes before connecting scene text or UI actions.

Publish request admission, synthesis, playback, cancellation and failure through
an extension of `DeviceSnapshotService`. Admission and generated PCM do not
prove audio playback completion. Cody/GUI consumers use that canonical state;
they do not inspect codec/storage/sensors or invent talking/freshness state.
The user explicitly requested Italian and Chinese speech: the TTS request
boundary therefore needs bounded, validated UTF-8 and an explicit language.
Reject malformed encoding, overlong input and disabled/unknown languages with
an original error; never truncate through a character, silently transliterate
or fall back to English. This is a TTS input requirement, not authorization to
localize Cody's existing English printable-ASCII UI/scene text. Keep runtime
measurements behind the canonical snapshot's validity rules.

The upstream engine/embedded port identifies GPL-3.0 licensing. Keep its source
provenance and license notices in any later vendored component; this checkpoint
adds no vendor code to the firmware.

## Next implementation and validation boundary

Implement the native optional component and curated three-language flash
registry first, together with the catalog/CLI compile-time selection and a continuous audio
session. SD packaging is a separate backend with explicit storage ownership.
Do not expose an enabled-but-nonfunctional firmware feature as completed work.

After implementation and static cross-component review, request authorization
for exact final commands through `scripts/device.ps1`. Required evidence will
include disabled/enabled build and flash-list exclusion, app/PSRAM/stack budgets,
continuous PCM, cancellation, standby, selected-backend failures and actual
acoustics. No tests, configure/build/size, scans, simulator, hardware/serial,
flash, SD provisioning or erase were performed in this study.

## Source references

- [eSpeak NG architecture and API](https://github.com/espeak-ng/espeak-ng),
  [synchronous PCM callback contract](https://github.com/espeak-ng/espeak-ng/blob/master/src/include/espeak-ng/speak_lib.h).
- Pinned embedded port:
  [README](https://github.com/pschatzmann/arduino-espeak-ng/blob/93b2651d324874e1e00077af008d8ab83ab32e73/README.md),
  [minimal data](https://github.com/pschatzmann/arduino-espeak-ng/tree/93b2651d324874e1e00077af008d8ab83ab32e73/espeak-ng-data-min),
  [full data for language selection](https://github.com/pschatzmann/arduino-espeak-ng/tree/93b2651d324874e1e00077af008d8ab83ab32e73/espeak-ng-data),
  [table loader](https://github.com/pschatzmann/arduino-espeak-ng/blob/93b2651d324874e1e00077af008d8ab83ab32e73/src/libespeak-ng/synthdata.c),
  [dictionary loader](https://github.com/pschatzmann/arduino-espeak-ng/blob/93b2651d324874e1e00077af008d8ab83ab32e73/src/libespeak-ng/dictionary.c),
  [allocator](https://github.com/pschatzmann/arduino-espeak-ng/blob/93b2651d324874e1e00077af008d8ab83ab32e73/src/libespeak-ng/mem_alloc.c),
  [API adaptation](https://github.com/pschatzmann/arduino-espeak-ng/blob/93b2651d324874e1e00077af008d8ab83ab32e73/arduino/espeak.h).
- Reference checkout only: `artifacts/research/luna-speak/arduino-espeak-ng`.
  This ignored checkout is disposable; the pinned links above retain provenance.
