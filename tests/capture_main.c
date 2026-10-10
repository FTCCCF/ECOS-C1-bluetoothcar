/* Simulation-only fixture. This is never a deployable car firmware. */
#include "main.h"
static int16_t capture_buffer[8];
int main(void) {
    c1_init(); c1_puts("CAPTURE_TEST_BEGIN\n");
    uint32_t elapsed=soft_i2s_alt_mono(capture_buffer, 2, 8, 0x85);
    for (unsigned i=0; i<8; ++i) {
        unsigned f=i+2;
        uint32_t pcm=(f%2 ? 0x123456u : 0x876543u)+f;
        if (capture_buffer[i]!=(int16_t)(pcm>>8)) {
            c1_puts("PCM_MISMATCH index="); c1_unsigned(i);
            c1_puts(" actual="); c1_unsigned((uint16_t)capture_buffer[i]);
            c1_puts(" expected="); c1_unsigned((uint16_t)(pcm>>8)); c1_puts("\nCAPTURE_TEST_FAIL\n"); for (;;) { }
        }
    }
    if ((REG_GPIO_0_DR & ~(1u<<VOICE_MIC_SD_GPIO))!=0x85 || !elapsed) { c1_puts("CAPTURE_TEST_FAIL\n"); for (;;) { } }
    c1_puts("CAPTURE_TEST_DONE\n"); for (;;) { }
}
