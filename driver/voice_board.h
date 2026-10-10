#ifndef VOICE_BOARD_H
#define VOICE_BOARD_H
#include <stdint.h>
#include "voice/motor.h"
#ifndef VOICE_DRIVE
#define VOICE_DRIVE 0
#endif
#ifndef VOICE_LCD
#define VOICE_LCD 1
#endif
#ifndef VOICE_MIC_SD_GPIO
#define VOICE_MIC_SD_GPIO 5
#endif
#define C1_CPU_HZ 72000000u
#define VOICE_CAPTURE_SAMPLES 24576u
#define VOICE_WARMUP_SAMPLES 65536u
#define VOICE_VAD_PRE_SAMPLES 2048u
typedef struct {
    uint32_t prefix_head, trigger_abs, noise_abs, wait_frames, aborted;
    uint32_t block_cycles[16];
} voice_capture_info_t;
_Static_assert(__builtin_offsetof(voice_capture_info_t, block_cycles)==20, "ASM capture layout");
_Static_assert(VOICE_CAPTURE_SAMPLES>VOICE_VAD_PRE_SAMPLES, "Capture prefix too large");
_Static_assert(VOICE_WARMUP_SAMPLES%128u==0, "Warmup must end on a VAD block");
_Static_assert(VOICE_VAD_PRE_SAMPLES==2048u, "ASM circular prefix size");
_Static_assert(VOICE_MIC_SD_GPIO<16, "Invalid microphone GPIO");
_Static_assert(((1u<<VOICE_MIC_SD_GPIO)&(MOTOR_DIRECTION_MASK|(1u<<2)|(1u<<8)|(1u<<9)))==0,
               "Microphone SD overlaps a motor/LCD/clock output");
void c1_init(void);
uint32_t c1_cycles(void);
uint32_t c1_gpio_output(void);
int c1_getchar(void);
void c1_puts(const char *value);
void c1_unsigned(uint32_t value);
void c1_apply_motor(const motor_state_t *state);
void c1_lcd_line(unsigned line, const char *value, uint16_t color);
void c1_disable_lcd(void);
void c1_dump_audio(const int16_t *audio, uint32_t samples, uint32_t elapsed);
uint32_t soft_i2s_alt_mono(void *buffer, uint32_t warmup, uint32_t samples, uint32_t gpio_base);
uint32_t soft_i2s_triggered_mono(void *buffer, uint32_t warmup, uint32_t samples,
                                 uint32_t gpio_base, voice_capture_info_t *info);
void c1_align_voice_prefix(int16_t *audio, uint32_t head);
#endif
