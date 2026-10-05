# Native eSpeak source provenance

Imported core and Unicode sources from pschatzmann/arduino-espeak-ng commit
`93b2651d324874e1e00077af008d8ab83ab32e73` (eSpeak NG 1.52-dev).
Source: https://github.com/pschatzmann/arduino-espeak-ng/tree/93b2651d324874e1e00077af008d8ab83ab32e73

Only the reviewed native synthesis C sources are compiled. Arduino classes,
AudioTools, POSIX emulation, emulator, desktop audio and asynchronous pthread
backends are excluded. Original license notices are retained. Local changes
route data through a verified immutable asset registry, guard allocation
failures in a serialized C boundary and preserve borrowed-data ownership.

Matched runtime data lives in assets/tts/espeak-ng with a SHA-256 manifest.
This is versioned input, not generated build output or evidence of validation.

Native changes include common.c (registry file sizes), voices.c (native
libc directory header and real voice-loading failures), translate.c (original
alternate-dictionary failures) and speech.c (explicit registry namespace and a
waveform seed independent of live clock reads). The imported platform allocator
implementations/header are excluded; the native component owns allocation.
The upstream C APIs are serialized behind the native service, not exposed as a
second public playback path. Runtime table generation and desktop APIs are unused.

The first ESP-IDF 6.0.2/GCC 15 build exposed additional native-source issues.
Dictionary identity copies now reject oversized names and preserve termination;
the two intentional dictionary stress/mnemonic fallthroughs are explicit.
Intentional Unicode property/whitespace, language-rule and waveform fallthroughs are also
explicit. Voice-score language/path buffers reject overflow without truncation.
Number dictionary keys and intermediate phonemes use the native checked-format
function, which stops the guarded engine on overflow instead of truncating.
Special/character pronunciations carry their actual destination capacity through
readclause.c. The speech dispatch flag is defined for the synchronous-only build.
The native configuration uses the toolchain's endian definitions without replacing
them. These are source corrections, not a waiver of compiler diagnostics.
Desktop dictionary trace blocks are excluded from the native build so that
precompiled dictionary synthesis does not link the runtime dictionary compiler.

After a captured boot failure creating the mandatory power task, the component's
IDF linker fragment places native engine BSS/COMMON in PSRAM. Its sole worker
already has a PSRAM stack and cannot execute with caches disabled. Public service
synchronization and the native allocation guard remain in internal RAM. This
placement is admitted only by the explicitly enabled speech component.
