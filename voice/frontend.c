#include "voice.h"
#include "model_config.h"

/* Single-threaded SRAM workspace; no allocation and no PSRAM accesses. */
static int32_t ring[400], real[512], imag[512], mfcc[98*12];
static uint64_t power[257];

typedef struct {
    const int16_t *pcm;
    uint32_t samples, step_q24, cutoff_q20;
    int32_t mean_q12;
    int64_t z1, z2;
} stream_t;

static uint32_t square_root(uint64_t value) {
    uint64_t result=0, bit=(uint64_t)1 << 62;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= result+bit) { value-=result+bit; result=(result >> 1)+bit; }
        else result >>= 1;
        bit >>= 2;
    }
    return (uint32_t)result;
}

static int32_t resampled(const stream_t *s, uint32_t sample) {
    uint64_t position=(uint64_t)sample*s->step_q24;
    int center=(int)(position >> 24);
    int32_t fraction=(int32_t)(position & 0xffffff);
    int64_t sum=0;
    for (int k=-11; k<=12; ++k) {
        int source=center+k;
        if (source < 0 || (uint32_t)source >= s->samples) continue;
        int32_t distance=k*16777216-fraction;
        uint32_t magnitude=distance < 0 ? (uint32_t)-distance : (uint32_t)distance;
        uint32_t x=(uint32_t)(((uint64_t)magnitude*s->cutoff_q20) >> 28); /* Q16 */
        if (x >= 10u*65536u) continue;
        uint32_t index=x >> 8, part=x & 255;
        int32_t coefficient=resample_kernel_q30[index]+voice_round_shift(
            (int64_t)(resample_kernel_q30[index+1]-resample_kernel_q30[index])*part, 8);
        sum+=(int64_t)s->pcm[source]*coefficient;
    }
    return voice_round_shift(sum, 18); /* raw PCM amplitude in Q12 */
}

static void filter_reset(stream_t *s, uint32_t start) {
    int32_t first=resampled(s, start)-s->mean_q12;
    s->z1=(int64_t)hp_zi_q29[0]*first;
    s->z2=(int64_t)hp_zi_q29[1]*first;
}

static int32_t filtered(stream_t *s, uint32_t sample) {
    int32_t x=resampled(s, sample)-s->mean_q12;
    int32_t y=voice_round_shift((int64_t)hp_q29[0]*x+s->z1, 29);
    s->z1=(int64_t)hp_q29[1]*x-(int64_t)hp_q29[3]*y+s->z2;
    s->z2=(int64_t)hp_q29[2]*x-(int64_t)hp_q29[4]*y;
    return y;
}

static void fft(void) {
    for (unsigned i=1, j=0; i<512; ++i) {
        unsigned bit=256;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { int32_t t=real[i]; real[i]=real[j]; real[j]=t; }
    }
    for (unsigned size=2; size<=512; size <<= 1) {
        unsigned half=size/2, stride=512/size;
        for (unsigned base=0; base<512; base+=size) for (unsigned k=0; k<half; ++k) {
            unsigned a=base+k, b=a+half, phase=k*stride;
            int32_t tr=voice_round_shift((int64_t)real[b]*twiddle_real_q30[phase]-(int64_t)imag[b]*twiddle_imag_q30[phase], 30);
            int32_t ti=voice_round_shift((int64_t)real[b]*twiddle_imag_q30[phase]+(int64_t)imag[b]*twiddle_real_q30[phase], 30);
            real[b]=real[a]-tr; imag[b]=imag[a]-ti;
            real[a]+=tr; imag[a]+=ti;
        }
    }
    for (unsigned i=0; i<=256; ++i)
        power[i]=(uint64_t)((int64_t)real[i]*real[i]+(int64_t)imag[i]*imag[i]);
}

static int32_t logarithm_q20(uint64_t energy_q32) {
    if (!energy_q32) return -24144355; /* ln(1e-10) in Q20 */
    unsigned exponent=0;
    for (uint64_t v=energy_q32; v>1; v >>= 1) ++exponent;
    uint32_t normalized=(uint32_t)(exponent>=16 ? energy_q32 >> (exponent-16) : energy_q32 << (16-exponent));
    uint32_t part=normalized-65536, index=part >> 8, fraction=part & 255;
    int32_t mantissa=ln_mantissa_q20[index]+voice_round_shift(
        (int64_t)(ln_mantissa_q20[index+1]-ln_mantissa_q20[index])*fraction, 8);
    return ((int32_t)exponent-32)*726817+mantissa;
}

int voice_frontend(const int16_t *pcm, uint32_t samples, uint32_t rate_q16,
                   uint32_t start_16k, int8_t input[VOICE_INPUTS], int32_t *debug) {
    if (!pcm || !input || rate_q16 < 15500u*65536u || rate_q16 > 18000u*65536u || samples>100000u) return -1;
    stream_t s={0};
    s.pcm=pcm; s.samples=samples;
    /* Match Fraction(16000/source_rate).limit_denominator(4096), used by
     * the training frontend. Compare integer errors without soft-float. */
    uint32_t up=1, down=1;
    uint64_t best_error=UINT64_MAX;
    for (uint32_t d=1; d<=4096; ++d) {
        uint64_t target=((uint64_t)16000 << 16)*d;
        uint32_t n=(uint32_t)((target+rate_q16/2)/rate_q16);
        uint64_t product=(uint64_t)n*rate_q16;
        uint64_t error=target>product ? target-product : product-target;
        if (best_error==UINT64_MAX || error*down<best_error*d) { up=n; down=d; best_error=error; }
    }
    s.step_q24=(uint32_t)((((uint64_t)down << 24)+up/2)/up);
    s.cutoff_q20=(uint32_t)(((uint64_t)up << 20)/down);
    if (s.cutoff_q20 > (1u << 20)) s.cutoff_q20=1u << 20;
    /* resample_poly rounds its output length upward; the final output
     * sample may therefore precede the source endpoint by less than a frame. */
    if (!samples || ((uint64_t)start_16k+15999u)*s.step_q24 >= ((uint64_t)samples << 24)) return -1;
    int64_t total=0;
    for (uint32_t i=0; i<16000; ++i) total+=resampled(&s, start_16k+i);
    s.mean_q12=(int32_t)(total/16000);
    filter_reset(&s, start_16k);
    uint64_t energy=0;
    for (uint32_t i=0; i<16000; ++i) {
        int32_t x=filtered(&s, start_16k+i);
        energy+=((uint64_t)((int64_t)x*x)) >> 16;
    }
    uint32_t rms_q12=square_root((energy/16000) << 16);
    if (!rms_q12) rms_q12=1;
    filter_reset(&s, start_16k);
    uint32_t next=0;
    for (unsigned frame=0; frame<98; ++frame) {
        if (frame) for (unsigned i=0; i<240; ++i) ring[i]=ring[i+160];
        for (unsigned i=frame ? 240 : 0; i<400; ++i, ++next)
            ring[i]=(int32_t)(((int64_t)filtered(&s, start_16k+next)*65536)/rms_q12);
        for (unsigned i=0; i<512; ++i) {
            real[i]=i<400 ? voice_round_shift((int64_t)ring[i]*hann_q15[i], 15) : 0;
            imag[i]=0;
        }
        fft();
        int32_t logmel[40];
        for (unsigned m=0; m<40; ++m) {
            uint64_t value=0;
            for (unsigned j=0; j<mel_count[m]; ++j) {
                uint64_t p=power[mel_first[m]+j];
                uint32_t w=mel_weight_q15[mel_offset[m]+j];
                value+=(p >> 15)*w+(((p & 32767)*w+16384) >> 15);
            }
            logmel[m]=logarithm_q20(value);
        }
        for (unsigned c=0; c<12; ++c) {
            int64_t value=0;
            for (unsigned m=0; m<40; ++m) value+=(int64_t)logmel[m]*dct_q24[c*40+m];
            mfcc[frame*12+c]=voice_round_shift(value, 28);
        }
    }
#if VOICE_CEPSTRAL_CENTER
    /* Remove a stationary channel offset over the same complete 98-frame
     * window as the training profile, before temporal interpolation. */
    for (unsigned c=0; c<12; ++c) {
        int64_t total=0;
        for (unsigned t=0; t<98; ++t) total+=mfcc[t*12+c];
        int32_t mean=(int32_t)(total/98);
        for (unsigned t=0; t<98; ++t) mfcc[t*12+c]-=mean;
    }
#endif
    for (unsigned t=0; t<48; ++t) {
        unsigned position=t*97, left=position/47, part=position%47;
        for (unsigned c=0; c<12; ++c) {
            int32_t value=mfcc[left*12+c];
            if (part) value+=(int32_t)(((int64_t)(mfcc[(left+1)*12+c]-value)*part)/47);
            if (debug) debug[t*12+c]=value;
            int32_t q=voice_round_shift((int64_t)(value-feature_mean_q16[c])*feature_gain_q24[c], 40);
            input[c*48+t]=(int8_t)(q < -127 ? -127 : q > 127 ? 127 : q);
        }
    }
    return 0;
}
