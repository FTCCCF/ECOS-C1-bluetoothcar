#ifndef VOICE_H
#define VOICE_H
#include <stdint.h>
#define VOICE_RATE 16000u
#define VOICE_INPUTS (48*12)
#define VOICE_CLASSES 5
#define VOICE_FORWARD 0
#define VOICE_STOP 1
#define VOICE_BACKWARD 2
#define VOICE_REJECT (-1)
#define VOICE_PROBABILITY_MIN 12582912u /* 0.75 in Q24 */
#define VOICE_MARGIN_MIN 2516583u      /* ceil(0.15 * 2^24) */
typedef struct {
    int32_t logits_q16[VOICE_CLASSES];
    uint32_t probability_q24[VOICE_CLASSES];
    int class_id;
    int decision;
} voice_result_t;
int voice_frontend(const int16_t *pcm, uint32_t samples, uint32_t rate_q16,
                   uint32_t start_16k, int8_t input[VOICE_INPUTS], int32_t *mfcc_q16);
void voice_infer(const int8_t input[VOICE_INPUTS], voice_result_t *result);
void voice_select(voice_result_t *best, const voice_result_t *candidate);
const char *voice_label(int id);
int32_t voice_round_shift(int64_t value, unsigned bits);
#endif
