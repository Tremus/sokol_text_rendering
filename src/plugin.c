#include "plugin.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <xhl/maths.h>
#include <xhl/thread.h>
#include <xhl/time.h>
#include <xhl/vector.h>

// Apparently denormals aren't a problem on ARM & M1?
// https://en.wikipedia.org/wiki/Subnormal_number
// https://www.kvraudio.com/forum/viewtopic.php?t=575799
#if __arm64__
#define DISABLE_DENORMALS
#define RESTORE_DENORMALS
#elif defined(_WIN32)
// https://softwareengineering.stackexchange.com/a/337251
#include <immintrin.h>
#define DISABLE_DENORMALS                                                                                              \
    unsigned int oldMXCSR = _mm_getcsr();       /*read the old MXCSR setting  */                                       \
    unsigned int newMXCSR = oldMXCSR |= 0x8040; /* set DAZ and FZ bits        */                                       \
    _mm_setcsr(newMXCSR);                       /* write the new MXCSR setting to the MXCSR */
#define RESTORE_DENORMALS _mm_setcsr(oldMXCSR);
#else
#include <fenv.h>
#define DISABLE_DENORMALS                                                                                              \
    fenv_t _fenv;                                                                                                      \
    fegetenv(&_fenv);                                                                                                  \
    fesetenv(FE_DFL_DISABLE_SSE_DENORMS_ENV);
#define RESTORE_DENORMALS fesetenv(&_fenv);
#endif

// Used for midi note maths
#define MIDI_NOTE_NUM_20Hz  15.486820576352429f // getMidiNoteFromHertz(20)
#define MIDI_NOTE_NUM_20kHz 135.0762319922975f  // getMidiNoteFromHertz(20000)
// getMidiNoteFromHertz(20000) - getMidiNoteFromHertz(20)
#define MIDI_NOTE_NUM_RANGE 119.58941141594507f

XTHREAD_LOCAL bool g_is_main_thread = false;
bool               is_main_thread() { return g_is_main_thread; }

void cplug_libraryLoad()
{
    xtime_init();
    xalloc_init();
}
void cplug_libraryUnload() { xalloc_shutdown(); }

extern void library_load_platform();
extern void library_unload_platform();

void* cplug_createPlugin(CplugHostContext* ctx)
{
    g_is_main_thread = true;
    library_load_platform();

    struct Plugin* p = MY_CALLOC(1, sizeof(*p));
    p->cplug_ctx     = ctx;

    p->width  = GUI_INIT_WIDTH;
    p->height = GUI_INIT_HEIGHT;

    return p;
}

void cplug_destroyPlugin(void* p)
{
    CPLUG_LOG_ASSERT(p != NULL);
    library_unload_platform();

    MY_FREE(p);
}

uint32_t cplug_getNumInputBusses(void* ptr) { return 1; }
uint32_t cplug_getNumOutputBusses(void* ptr) { return 1; }
uint32_t cplug_getInputBusChannelCount(void* p, uint32_t bus_idx) { return 2; }
uint32_t cplug_getOutputBusChannelCount(void* p, uint32_t bus_idx) { return 2; }

void cplug_getInputBusName(void* ptr, uint32_t idx, char* buf, size_t buflen) { snprintf(buf, buflen, "Audio Input"); }
void cplug_getOutputBusName(void* ptr, uint32_t idx, char* buf, size_t buflen) { snprintf(buf, buflen, "Audio Output"); }

uint32_t cplug_getNumParameters(void* p) { return 0; }
uint32_t cplug_getParameterID(void* p, uint32_t paramIndex) { return paramIndex; }
uint32_t cplug_getParameterFlags(void* p, uint32_t paramId) { return 0; }
// NOTE: AUv2 supports a max length of 52 bytes, VST3 128, CLAP 256
void cplug_getParameterName(void* p, uint32_t paramId, char* buf, size_t buflen) { snprintf(buf, buflen, "N/A"); }

void cplug_getParameterRange(void* p, uint32_t paramId, double* min, double* max)
{
    *min = 0;
    *max = 1;
}

double cplug_getDefaultParameterValue(void* _p, uint32_t paramId) { return 0; }
double cplug_getParameterValue(void* _p, uint32_t paramId) { return 0; }
// [hopefully audio thread] VST3 & AU only
void cplug_setParameterValue(void* _p, uint32_t paramId, double value) {}
// VST3 only
double cplug_denormaliseParameterValue(void* _p, uint32_t id, double v) { return v; }
double cplug_normaliseParameterValue(void* _p, uint32_t id, double v) { return v; }
double cplug_parameterStringToValue(void* _p, uint32_t id, const char* str) { return 0; }
void   cplug_parameterValueToString(void* _p, uint32_t id, char* b, size_t sz, double v) { snprintf(b, sz, "%f", v); }

uint32_t cplug_getLatencyInSamples(void* p) { return 0; }
uint32_t cplug_getTailInSamples(void* p) { return 0; }

void cplug_setSampleRateAndBlockSize(void* _p, double sampleRate, uint32_t maxBlockSize)
{
    Plugin* p         = _p;
    p->sample_rate    = sampleRate;
    p->max_block_size = maxBlockSize;
}

void cplug_process(void* _p, CplugProcessContext* ctx)
{
    DISABLE_DENORMALS
    Plugin* p = _p;

    // NOTE: FL studio may return NULL if your FX slot is bypassed
    float** output = ctx->getAudioOutput(ctx, 0);
    CPLUG_LOG_ASSERT(output != NULL);
    CPLUG_LOG_ASSERT(output[0] != NULL);
    CPLUG_LOG_ASSERT(output[1] != NULL);

    // Force "in place processing"
    {
        float** input = ctx->getAudioInput(ctx, 0);
        if (input && output && input[0] != output[0])
        {
            memcpy(output[0], input[0], sizeof(float) * ctx->numFrames);
            memcpy(output[1], input[1], sizeof(float) * ctx->numFrames);
        }
    }

    if (output)
    {
        if (output[0])
        {
            memset(output[0], 0, sizeof(float) & ctx->numFrames);
        }
        if (output[1])
        {
            memcpy(output[1], output[0], sizeof(float) * ctx->numFrames);
        }
    }

    RESTORE_DENORMALS
}

void cplug_saveState(void* _p, const void* stateCtx, cplug_writeProc writeProc) {}
void cplug_loadState(void* _p, const void* stateCtx, cplug_readProc readProc) {}