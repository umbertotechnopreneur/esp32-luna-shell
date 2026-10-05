#include "native_engine.h"
#include "mem_alloc.h"
#include <espeak-ng/espeak_ng.h>
#include <espeak-ng/speak_lib.h>

static int (*pcm_sink)(short *, int, void *);
typedef struct {
    const char *voice_path;
    const char *text;
    size_t bytes;
    int rate;
    int pitch;
    int range;
} EngineRequest;

// samples: Borrowed engine buffer, valid only during this callback.
// count: Actual signed PCM16 sample count.
// events: Engine event list, unused by the platform audio owner.
static int receive_pcm(short *samples, int count, espeak_EVENT *events)
{
    return pcm_sink(samples, count, events);
}

// context: Unused guarded initialization context.
// Exception: Returns the original engine cause without legacy exit-on-error.
static int initialize_engine(void *context)
{
    (void)context;
    espeak_ng_InitializePath("/luna-tts/espeak-ng-data");
    espeak_ng_STATUS status = espeak_ng_Initialize(NULL);
    if (status != ENS_OK) return status;
    return espeak_ng_InitializeOutput(ENOUTPUT_MODE_SYNCHRONOUS, 60, NULL);
}

// No parameters: only the sole worker may enter the guarded engine.
int luna_tts_engine_initialize(void)
{
    return luna_espeak_guard(initialize_engine, NULL);
}

// context: Trivial request, owned by the calling sole worker.
// Exception: Preserves original voice, parameter and synthesis statuses.
static int synthesize_engine(void *context)
{
    const EngineRequest *request = context;
    espeak_ng_STATUS status = espeak_ng_SetVoiceByFile(request->voice_path);
    if (status != ENS_OK) return status;
    status = espeak_ng_SetParameter(espeakRATE, request->rate, 0);
    if (status != ENS_OK) return status;
    status = espeak_ng_SetParameter(espeakPITCH, request->pitch, 0);
    if (status != ENS_OK) return status;
    status = espeak_ng_SetParameter(espeakRANGE, request->range, 0);
    if (status != ENS_OK) return status;
    espeak_SetSynthCallback(receive_pcm);
    return espeak_ng_Synthesize(request->text, request->bytes, 0, POS_CHARACTER,
                               0, espeakCHARS_UTF8, NULL, NULL);
}

// voice_path: Exact package-owned language filename.
// text: Bounded validated UTF-8, including a terminator.
// bytes: Exact size including the terminator.
// rate: Requested word rate.
// pitch: Requested pitch.
// range: Requested pitch range.
// callback: Borrowed synchronous sink; never invokes engine allocations itself.
int luna_tts_engine_synthesize(const char *voice_path, const char *text, size_t bytes,
                              int rate, int pitch, int range,
                              int (*callback)(short *, int, void *))
{
    EngineRequest request = {voice_path, text, bytes, rate, pitch, range};
    pcm_sink = callback;
    return luna_espeak_guard(synthesize_engine, &request);
}

// No parameters: valid only after successful initialization.
int luna_tts_engine_sample_rate(void)
{
    return espeak_ng_GetSampleRate();
}
