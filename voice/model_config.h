#ifndef VOICE_MODEL_CONFIG_H
#define VOICE_MODEL_CONFIG_H
/* Candidate builds select their own immutable headers; the original export
 * remains the default for reproducing the original port evidence. */
#ifndef VOICE_MODEL_HEADER
#define VOICE_MODEL_HEADER "voice/generated/model.h"
#endif
#ifndef VOICE_DSP_HEADER
#define VOICE_DSP_HEADER "voice/generated/dsp.h"
#endif
#include VOICE_MODEL_HEADER
#include VOICE_DSP_HEADER
#ifndef VOICE_CEPSTRAL_CENTER
#define VOICE_CEPSTRAL_CENTER 0
#endif
#ifndef VOICE_FINAL_POOL_BINS
#define VOICE_FINAL_POOL_BINS 8
#endif
#if VOICE_FINAL_POOL_BINS != 1 && VOICE_FINAL_POOL_BINS != 8
#error Unsupported temporal classifier head
#endif
#ifndef VOICE_DSP_PRECISION
#define VOICE_DSP_PRECISION 16
#endif
#if VOICE_DSP_PRECISION != 16
#error Only the verified original DSP precision is supported
#endif
#endif
