#include "voice.h"
#include "model_config.h"

static uint8_t clip_relu(int32_t value) { return (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value); }

void voice_infer(const int8_t input[VOICE_INPUTS], voice_result_t *r) {
    uint8_t first[16*48], pool1[16*24], second[32*24], pool2[32*VOICE_FINAL_POOL_BINS];
    _Static_assert(sizeof(w3)==5*32*VOICE_FINAL_POOL_BINS, "Classifier head/header mismatch");
    for (unsigned o=0; o<16; ++o) for (unsigned t=0; t<48; ++t) {
        int32_t acc=b1[o];
        for (unsigned c=0; c<12; ++c) for (unsigned k=0; k<5; ++k) {
            int p=(int)t+(int)k-2;
            if (p>=0 && p<48) acc+=(int32_t)input[c*48+(unsigned)p]*w1[(o*12+c)*5+k];
        }
        first[o*48+t]=clip_relu(voice_round_shift((int64_t)acc*requant1_q32[o], 32));
    }
    for (unsigned c=0; c<16; ++c) for (unsigned t=0; t<24; ++t)
        pool1[c*24+t]=(uint8_t)voice_round_shift((int32_t)first[c*48+t*2]+first[c*48+t*2+1], 1);
    for (unsigned o=0; o<32; ++o) for (unsigned t=0; t<24; ++t) {
        int32_t acc=b2[o];
        for (unsigned c=0; c<16; ++c) for (unsigned k=0; k<5; ++k) {
            int p=(int)t+(int)k-2;
            if (p>=0 && p<24) acc+=(int32_t)pool1[c*24+(unsigned)p]*w2[(o*16+c)*5+k];
        }
        second[o*24+t]=clip_relu(voice_round_shift((int64_t)acc*requant2_q32[o], 32));
    }
    for (unsigned c=0; c<32; ++c) for (unsigned t=0; t<VOICE_FINAL_POOL_BINS; ++t) {
        const unsigned width=24/VOICE_FINAL_POOL_BINS;
        unsigned sum=0;
        for (unsigned k=0; k<width; ++k) sum+=second[c*24+t*width+k];
        unsigned value=sum/width, remainder=sum%width;
        /* Match NumPy ties-to-even, including the even-width global pool. */
        if (remainder*2>width || (remainder*2==width && (value&1))) ++value;
        pool2[c*VOICE_FINAL_POOL_BINS+t]=(uint8_t)value;
    }
    int32_t maximum=-2147483647;
    for (unsigned o=0; o<5; ++o) {
        int32_t acc=b3[o];
        for (unsigned i=0; i<32*VOICE_FINAL_POOL_BINS; ++i)
            acc+=(int32_t)pool2[i]*w3[o*32*VOICE_FINAL_POOL_BINS+i];
        r->logits_q16[o]=voice_round_shift((int64_t)acc*logit_scale_q40[o], 24);
        if (r->logits_q16[o]>maximum) maximum=r->logits_q16[o];
    }
    uint32_t exponent[5], total=0;
    for (unsigned c=0; c<5; ++c) {
        uint32_t distance=(uint32_t)(maximum-r->logits_q16[c]);
        if (distance>=16u*65536u) exponent[c]=0;
        else {
            unsigned index=distance >> 10, part=distance & 1023;
            exponent[c]=exp_negative_q24[index]+voice_round_shift(
                (int64_t)((int32_t)exp_negative_q24[index+1]-(int32_t)exp_negative_q24[index])*part, 10);
        }
        total+=exponent[c];
    }
    uint32_t top=0, runner=0;
    r->class_id=0;
    for (unsigned c=0; c<5; ++c) {
        uint32_t p=(uint32_t)(((uint64_t)exponent[c] << 24)/total);
        r->probability_q24[c]=p;
        if (p>top) { runner=top; top=p; r->class_id=(int)c; }
        else if (p>runner) runner=p;
    }
    r->decision=r->class_id<3 && top>=VOICE_PROBABILITY_MIN && top-runner>=VOICE_MARGIN_MIN ? r->class_id : VOICE_REJECT;
}

void voice_select(voice_result_t *best, const voice_result_t *candidate) {
    if (best->decision==VOICE_STOP) return;
    if (candidate->decision==VOICE_STOP || (candidate->decision>=0 &&
        (best->decision<0 || candidate->probability_q24[candidate->class_id]>best->probability_q24[best->class_id]))) *best=*candidate;
}

const char *voice_label(int id) {
    static const char *const labels[]={"forward", "stop", "backward", "unknown", "noise"};
    return id>=0 && id<5 ? labels[id] : "reject";
}
