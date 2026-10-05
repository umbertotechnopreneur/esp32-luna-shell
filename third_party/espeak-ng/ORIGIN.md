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

Native changes are limited to common.c (registry file sizes), voices.c (native
newlib directory header and real voice-loading failures), translate.c (original
alternate-dictionary failures) and speech.c (explicit registry namespace and a
waveform seed independent of live clock reads). The imported platform allocator
implementations/header are excluded; the native component owns allocation.
The upstream C APIs are serialized behind the native service, not exposed as a
second public playback path. Runtime table generation and desktop APIs are unused.
