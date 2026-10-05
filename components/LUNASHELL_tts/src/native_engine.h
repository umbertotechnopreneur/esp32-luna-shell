#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// No parameters: initialize the synchronous engine under its allocator guard.
// Exception: Returns original eSpeak/errno status, without exiting or asserting.
int luna_tts_engine_initialize(void);
// voice_path: Exact manifest-owned language definition path.
// text: Validated null-terminated UTF-8 owned until synthesis returns.
// bytes: Exact input byte count including terminator.
// rate: Explicit bounded words-per-minute.
// pitch: Explicit 0..100 pitch.
// range: Explicit 0..100 pitch range.
// callback: Synchronous borrowed PCM16 sink; nonzero stops synthesis.
// Exception: Returns original eSpeak/errno status, including genuine stop/ENOMEM.
int luna_tts_engine_synthesize(const char *voice_path, const char *text, size_t bytes,
                              int rate, int pitch, int range,
                              int (*callback)(short *, int, void *));
// No parameters: sample rate from successfully loaded phonetic data.
int luna_tts_engine_sample_rate(void);
#ifdef __cplusplus
}
#endif
