/* Export the exact microphone buffer used by the preceding board inference. */
#include "main.h"

static void hex(uint32_t value, unsigned digits) {
    static const char chars[]="0123456789ABCDEF";
    while (digits) {
        char c[2]={chars[(value>>(--digits*4))&15u],0}; c1_puts(c);
    }
}
static void number(int32_t value) {
    if (value<0) { c1_puts("-"); c1_unsigned((uint32_t)(-(int64_t)value)); }
    else c1_unsigned((uint32_t)value);
}
void c1_dump_audio(const int16_t *audio, uint32_t samples, uint32_t elapsed) {
    if (!elapsed || !samples) { c1_puts("ERROR no_saved_audio\nREADY\n"); return; }
    int32_t minimum=32767,maximum=-32768,sum=0;
    uint64_t squares=0; uint32_t fnv=2166136261u;
    for (uint32_t i=0;i<samples;++i) {
        int32_t v=audio[i];
        if (v<minimum) minimum=v;
        if (v>maximum) maximum=v;
        sum+=v; squares+=(uint32_t)(v*v);
        fnv=(fnv^(uint8_t)v)*16777619u;
        fnv=(fnv^(uint8_t)((uint16_t)v>>8))*16777619u;
    }
    c1_puts("MIC_BEGIN samples="); c1_unsigned(samples);
    c1_puts(" rate_millihz="); c1_unsigned((uint32_t)((uint64_t)samples*C1_CPU_HZ*1000u/elapsed));
    c1_puts(" capture_cycles="); c1_unsigned(elapsed);
    c1_puts(" min="); number(minimum); c1_puts(" max="); number(maximum);
    c1_puts(" mean="); number(sum/(int32_t)samples);
    c1_puts(" mean_square="); c1_unsigned((uint32_t)(squares/samples)); c1_puts("\n");
    for (uint32_t offset=0;offset<samples;offset+=32) {
        c1_puts("PCM index="); c1_unsigned(offset); c1_puts(" data=");
        uint32_t end=offset+32; if (end>samples) end=samples;
        for (uint32_t i=offset;i<end;++i) hex((uint16_t)audio[i],4);
        c1_puts("\n");
    }
    c1_puts("MIC_END fnv="); hex(fnv,8); c1_puts("\nREADY\n");
}
