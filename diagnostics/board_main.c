/* Real-board hardware diagnosis. No speech inference or automatic motion. */
#include "main.h"

static int16_t audio[VOICE_CAPTURE_SAMPLES];
static motor_state_t motors;

static void signed_number(int32_t value) {
    if (value<0) { c1_puts("-"); c1_unsigned((uint32_t)(-(int64_t)value)); }
    else c1_unsigned((uint32_t)value);
}
static void hex_number(uint32_t value, unsigned digits) {
    static const char hex[]="0123456789ABCDEF";
    while (digits) {
        char character[2]={hex[(value>>(--digits*4))&15u],0};
        c1_puts(character);
    }
}
static void stop(void) {
    motor_command(&motors,VOICE_STOP,1); c1_apply_motor(&motors);
}
static void state(void) {
    c1_puts("STATE GPIO_shadow="); hex_number(c1_gpio_output(),8);
    c1_puts(" GPIO_live="); hex_number(REG_GPIO_0_DR,8);
    c1_puts(" DDR="); hex_number(REG_GPIO_0_DDR,8);
    c1_puts(" PWM_CTRL="); c1_unsigned(REG_PWM_0_CTRL);
    c1_puts(" PWM_CMP="); c1_unsigned(REG_PWM_0_CMP);
    c1_puts(" CR2_left="); c1_unsigned(REG_PWM_0_CR2);
    c1_puts(" CR1_right="); c1_unsigned(REG_PWM_0_CR1);
    c1_puts(" PWM_CNT="); c1_unsigned(REG_PWM_0_CNT); c1_puts("\n");
}
static void info(void) {
    c1_puts("C1 HARDWARE DIAGNOSTIC v3\n");
    c1_puts("boot=stopped duration_ms=3000 right_startup_ms=");
    c1_unsigned(MOTOR_RIGHT_STARTUP_MS); c1_puts("\n");
    c1_puts("i=info l=left_forward h=right_forward t=both_forward b=both_backward m=microphone s=stop\n");
    c1_puts("L=left_full H=right_full T=both_full B=both_reverse_full r=right_reverse R=right_reverse_full\n");
    c1_puts("SCK=H55.3 WS=H55.5 SD=H55.4 VDD=H55.30 GND=H55.28 LR=GND\n");
    state(); c1_puts("READY\n");
}
static void motor_test(int command) {
    int backward=command=='b' || command=='B' || command=='r' || command=='R';
    int left_only=command=='l' || command=='L';
    int right_only=command=='h' || command=='H' || command=='r' || command=='R';
    int full=command=='L' || command=='H' || command=='T' || command=='B' || command=='R';
    stop();
    motor_command(&motors,backward ? VOICE_BACKWARD : VOICE_FORWARD,1);
    if (left_only) motors.right=0;
    if (right_only) motors.left=0;
    motor_command(&motors,VOICE_REJECT,1);
    /* Full drive is an explicit bounded diagnostic, not a car speed change. */
    if (full) {
        if (motors.left) motors.cr2_left=0;
        if (motors.right) motors.cr1_right=0;
    }
    c1_puts("MOTOR_BEGIN target=");
    c1_puts(left_only ? "left" : right_only ? "right" : "both");
    c1_puts(backward ? " direction=backward" : " direction=forward");
    c1_puts(full ? " full=1\n" : " full=0\n");
    c1_apply_motor(&motors); state();
    uint32_t started=c1_cycles();
    while (c1_cycles()-started < 3u*C1_CPU_HZ) { }
    uint32_t count_before=REG_PWM_0_CNT;
    started=c1_cycles();
    while (c1_cycles()-started < 7200u) { }
    uint32_t count_after=REG_PWM_0_CNT;
    stop();
    c1_puts("MOTOR_END stopped=1 cnt_before="); c1_unsigned(count_before);
    c1_puts(" cnt_after="); c1_unsigned(count_after); c1_puts("\n");
    state(); c1_puts("READY\n");
}
static void microphone_test(void) {
    stop();
    c1_puts("MIC_LISTEN warmup_then_capture motors_stopped=1\n");
    uint32_t elapsed=soft_i2s_alt_mono(audio,VOICE_WARMUP_SAMPLES,
                                    VOICE_CAPTURE_SAMPLES,c1_gpio_output());
    if (!elapsed) { c1_puts("ERROR capture_cycles\nREADY\n"); return; }
    int32_t minimum=32767, maximum=-32768, sum=0;
    uint64_t squares=0;
    uint32_t fnv=2166136261u;
    for (unsigned i=0;i<VOICE_CAPTURE_SAMPLES;++i) {
        int32_t value=audio[i];
        if (value<minimum) minimum=value;
        if (value>maximum) maximum=value;
        sum+=value; squares+=(uint32_t)(value*value);
        fnv=(fnv^(uint8_t)value)*16777619u;
        fnv=(fnv^(uint8_t)((uint16_t)value>>8))*16777619u;
    }
    c1_puts("MIC_BEGIN samples="); c1_unsigned(VOICE_CAPTURE_SAMPLES);
    c1_puts(" rate_millihz="); c1_unsigned((uint32_t)((uint64_t)VOICE_CAPTURE_SAMPLES*C1_CPU_HZ*1000u/elapsed));
    c1_puts(" capture_cycles="); c1_unsigned(elapsed);
    c1_puts(" min="); signed_number(minimum);
    c1_puts(" max="); signed_number(maximum);
    c1_puts(" mean="); signed_number(sum/(int32_t)VOICE_CAPTURE_SAMPLES);
    c1_puts(" mean_square="); c1_unsigned((uint32_t)(squares/VOICE_CAPTURE_SAMPLES));
    c1_puts("\n");
    for (unsigned offset=0;offset<VOICE_CAPTURE_SAMPLES;offset+=32) {
        c1_puts("PCM index="); c1_unsigned(offset); c1_puts(" data=");
        unsigned end=offset+32;
        if (end>VOICE_CAPTURE_SAMPLES) end=VOICE_CAPTURE_SAMPLES;
        for (unsigned i=offset;i<end;++i) hex_number((uint16_t)audio[i],4);
        c1_puts("\n");
    }
    c1_puts("MIC_END fnv="); hex_number(fnv,8); c1_puts("\n");
    state(); c1_puts("READY\n");
}
int main(void) {
    c1_init(); stop(); info();
    for (;;) {
        int command=c1_getchar();
        if (command=='i') info();
        else if (command=='s') { stop(); c1_puts("STOPPED\n"); state(); c1_puts("READY\n"); }
        else if (command=='l' || command=='h' || command=='t' || command=='b' ||
                 command=='L' || command=='H' || command=='T' || command=='B' ||
                 command=='r' || command=='R') motor_test(command);
        else if (command=='m') microphone_test();
    }
}
