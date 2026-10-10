/* Simulation-only voice-onset/stop fixture; never flash this binary. */
#include "main.h"
static int16_t pcm[4096];
static voice_capture_info_t captured;
int main(void) {
    c1_init(); c1_puts("TRIGGER_CAPTURE_TEST_BEGIN\n");
    uint32_t elapsed=soft_i2s_triggered_mono(pcm, 256, 4096, 0x85, &captured);
    if ((REG_GPIO_0_DR & ~(1u<<VOICE_MIC_SD_GPIO))!=0x85) {
        c1_puts("TRIGGER_CAPTURE_TEST_FAIL gpio\n"); for (;;) { }
    }
    if (captured.aborted) {
        if (elapsed) { c1_puts("TRIGGER_CAPTURE_TEST_FAIL abort_cycles\n"); for (;;) { } }
        c1_puts("TRIGGER_CAPTURE_TEST_STOP\n"); for (;;) { }
    }
    if (!elapsed || captured.prefix_head>=2048 || captured.wait_frames<3840) {
        c1_puts("TRIGGER_CAPTURE_TEST_FAIL onset\n"); for (;;) { }
    }
    c1_align_voice_prefix(pcm, captured.prefix_head);
    for (unsigned i=0;i<4096;++i) {
        /* Stimulus is quiet through frame 4095. Trigger occurs only after
         * six voiced blocks. An earlier two-block burst must not trigger.
         * The circular prefix must remain in chronological order. */
        int16_t expected=i<1280 ? 0 : (i&1 ? -3000 : 3000);
        if (pcm[i]!=expected) {
            c1_puts("TRIGGER_CAPTURE_TEST_FAIL pcm_index="); c1_unsigned(i);
            c1_puts(" value="); c1_unsigned((uint16_t)pcm[i]); c1_puts("\n"); for (;;) { }
        }
    }
    c1_puts("TRIGGER_CAPTURE_TEST_DONE\n"); for (;;) { }
}
