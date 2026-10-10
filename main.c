#include "main.h"
#include "voice/model_config.h"
#ifndef VOICE_FIRMWARE_VERSION
#define VOICE_FIRMWARE_VERSION "v9"
#endif
#ifdef VOICE_SELFTEST
#ifndef VOICE_SELFTEST_HEADER
#define VOICE_SELFTEST_HEADER "tests/generated/selftest_pcm.h"
#endif
#include VOICE_SELFTEST_HEADER
#else
static int16_t audio[VOICE_CAPTURE_SAMPLES];
static uint32_t last_capture_cycles;
static voice_capture_info_t capture_info;
#endif
static int8_t input[VOICE_INPUTS];
static motor_state_t motors;

static void info(void) {
    c1_puts("C1 VOICE CAR " VOICE_FIRMWARE_VERSION "\nMODEL_SHA256=" VOICE_MODEL_SHA256 "\n");
    c1_puts(VOICE_CEPSTRAL_CENTER ? "cepstral_center=1\n" : "cepstral_center=0\n");
    c1_puts("final_pool_bins="); c1_unsigned(VOICE_FINAL_POOL_BINS); c1_puts("\n");
    c1_puts("dsp_precision="); c1_unsigned(VOICE_DSP_PRECISION); c1_puts("\n");
    c1_puts(VOICE_DRIVE ? "mode=car\n" : "mode=model_only PWM_DISABLED\n");
    if (VOICE_DRIVE) {
        c1_puts(VOICE_MOTOR_FULL_DUTY ? "motor_duty=full\n" : "motor_duty=original_pwm\n");
        c1_puts(VOICE_LEFT_CHANNEL_A ? "motor_wiring=left_A,right_B\n" : "motor_wiring=left_B,right_A\n");
        if (!VOICE_MOTOR_FULL_DUTY) {
            c1_puts("right_startup_ms="); c1_unsigned(MOTOR_RIGHT_STARTUP_MS); c1_puts("\n");
        }
    }
    c1_puts("classes=forward,stop,backward,unknown,noise\n");
    c1_puts("capture=voice_onset pre_samples=2048 voice_blocks=6 model_start_16k=0\n");
    c1_puts("SCK=H55.3 WS=H55.5 SD_GPIO="); c1_unsigned(VOICE_MIC_SD_GPIO);
    c1_puts(" default_SD=H55.4 VDD=H55.30 GND=H55.28 LR=H55.29\n");
    c1_puts("i=info a=one_capture r=continuous s=stop_and_pause d=last_audio q=lcd_off\nREADY\n");
}
static void report(const voice_result_t *r, uint32_t elapsed) {
    c1_puts("RESULT class="); c1_puts(voice_label(r->class_id));
    c1_puts(" decision="); c1_puts(voice_label(r->decision));
    c1_puts(" p_permille="); c1_unsigned((uint32_t)(((uint64_t)r->probability_q24[r->class_id]*1000)>>24));
    c1_puts(" infer_cycles="); c1_unsigned(elapsed);
    c1_puts(" CR2_B="); c1_unsigned(motors.cr2_left);
    c1_puts(" CR1_A="); c1_unsigned(motors.cr1_right);
    c1_puts(" p_forward="); c1_unsigned((uint32_t)(((uint64_t)r->probability_q24[0]*1000)>>24));
    c1_puts(" p_stop="); c1_unsigned((uint32_t)(((uint64_t)r->probability_q24[1]*1000)>>24));
    c1_puts(" p_backward="); c1_unsigned((uint32_t)(((uint64_t)r->probability_q24[2]*1000)>>24));
    c1_puts(" p_unknown="); c1_unsigned((uint32_t)(((uint64_t)r->probability_q24[3]*1000)>>24));
    c1_puts(" p_noise="); c1_unsigned((uint32_t)(((uint64_t)r->probability_q24[4]*1000)>>24));
    c1_puts("\n");
    c1_lcd_line(3, voice_label(r->decision), r->decision<0 ? 0xffe0 : 0x07e0);
}
static void run_model(const int16_t *pcm, uint32_t samples, uint32_t rate_q16, uint32_t start) {
    uint32_t begin=c1_cycles();
    if (voice_frontend(pcm, samples, rate_q16, start, input, 0)) {
        motor_command(&motors, VOICE_STOP, VOICE_DRIVE); c1_apply_motor(&motors);
        c1_puts("ERROR incomplete_window_or_rate\n"); return;
    }
    voice_result_t result;
    voice_infer(input, &result);
    uint32_t elapsed=c1_cycles()-begin;
    motor_command(&motors, result.decision, VOICE_DRIVE); c1_apply_motor(&motors);
    report(&result, elapsed);
}
int main(void) {
    c1_init(); motor_command(&motors, VOICE_STOP, VOICE_DRIVE); c1_apply_motor(&motors); info();
#ifdef VOICE_SELFTEST
    run_model(selftest_pcm, SELFTEST_SAMPLES, SELFTEST_RATE_Q16, SELFTEST_START_16K);
    c1_puts("SELFTEST_DONE\n");
    for (;;) { }
#else
    int continuous=1;
    for (;;) {
        int command=c1_getchar();
        if (command=='i') info();
        if (command=='s') {
            continuous=0; motor_command(&motors, VOICE_STOP, VOICE_DRIVE); c1_apply_motor(&motors);
            c1_puts("PAUSED motors_stopped\nREADY\n"); c1_lcd_line(3, "PAUSED", 0xffe0);
        }
        if (command=='r') continuous=1;
        if (command=='d') {
            continuous=0; motor_command(&motors, VOICE_STOP, VOICE_DRIVE); c1_apply_motor(&motors);
            c1_dump_audio(audio, VOICE_CAPTURE_SAMPLES, last_capture_cycles); continue;
        }
        if (command=='q') {
            c1_disable_lcd(); c1_puts("LCD_DISABLED GPIO2_LOW\nREADY\n");
        }
        if (!continuous && command!='a') continue;
        c1_lcd_line(2, "LISTEN", 0xffff); c1_puts("LISTEN warmup_then_wait_for_voice\n");
        uint32_t elapsed=soft_i2s_triggered_mono(audio, VOICE_WARMUP_SAMPLES,
                                                VOICE_CAPTURE_SAMPLES, c1_gpio_output(), &capture_info);
        last_capture_cycles=elapsed;
        if (!elapsed) {
            continuous=0;
            motor_command(&motors, VOICE_STOP, VOICE_DRIVE); c1_apply_motor(&motors);
            c1_puts("PAUSED motors_stopped\nREADY\n"); continue;
        }
        if (capture_info.prefix_head>=VOICE_VAD_PRE_SAMPLES) {
            continuous=0; motor_command(&motors, VOICE_STOP, VOICE_DRIVE); c1_apply_motor(&motors);
            c1_puts("ERROR capture_prefix\nREADY\n"); continue;
        }
        c1_align_voice_prefix(audio, capture_info.prefix_head);
        uint32_t rate_q16=(uint32_t)(((uint64_t)VOICE_CAPTURE_SAMPLES*C1_CPU_HZ*65536)/elapsed);
        if (rate_q16<15500u*65536u || rate_q16>18000u*65536u) {
            motor_command(&motors, VOICE_STOP, VOICE_DRIVE); c1_apply_motor(&motors);
            c1_puts("ERROR unsupported_capture_rate\n"); continue;
        }
        c1_puts("AUDIO samples="); c1_unsigned(VOICE_CAPTURE_SAMPLES);
        c1_puts(" rate_millihz="); c1_unsigned((uint32_t)(((uint64_t)rate_q16*1000)>>16));
        c1_puts(" model_start_16k=0 trigger_abs="); c1_unsigned(capture_info.trigger_abs);
        c1_puts(" noise_abs="); c1_unsigned(capture_info.noise_abs);
        c1_puts(" wait_frames="); c1_unsigned(capture_info.wait_frames); c1_puts("\n");
        uint32_t target_samples=(uint32_t)(((uint64_t)VOICE_CAPTURE_SAMPLES*VOICE_RATE*65536)/rate_q16);
        if (target_samples<VOICE_RATE) { motor_command(&motors, VOICE_STOP, VOICE_DRIVE); c1_apply_motor(&motors); continue; }
        c1_lcd_line(2, "INFER", 0xffff);
        /* Prefix retains the quiet-to-word onset. Take the first complete
         * second rather than cutting the detected word with a center crop. */
        run_model(audio, VOICE_CAPTURE_SAMPLES, rate_q16, 0);
        c1_puts("READY\n");
    }
#endif
}
