#include <assert.h>
#include <stdio.h>
#include <limits.h>
#include "voice/voice.h"
#include "voice/motor.h"

int main(void) {
    motor_state_t state={0};
    motor_command(&state, VOICE_FORWARD, 0);
    assert(!state.direction_bits && state.cr2_left==20000 && state.cr1_right==20000);
    motor_command(&state, VOICE_FORWARD, 1);
    assert(state.left==4000 && state.right==4000);
    assert(state.direction_bits==0x41);
#if VOICE_MOTOR_FULL_DUTY
    assert(state.cr2_left==0 && state.cr1_right==0);
#elif VOICE_LEFT_CHANNEL_A
    assert(state.cr2_left==6667 && state.cr1_right==7307);
#else
    assert(state.cr2_left==7307 && state.cr1_right==6667);
#endif
    motor_command(&state, VOICE_REJECT, 1);
    assert(state.left==4000 && state.direction_bits==0x41);
    motor_command(&state, VOICE_BACKWARD, 1);
    assert(state.left==-4000 && state.direction_bits==0x82);
    assert(state.cr2_left==(VOICE_MOTOR_FULL_DUTY ? 0 : 6667) && state.cr1_right==(VOICE_MOTOR_FULL_DUTY ? 0 : 6667));
    motor_command(&state, VOICE_STOP, 1);
    assert(!state.left && !state.right && !state.direction_bits && state.cr2_left==20000 && state.cr1_right==20000);
    state.left=4000; state.right=0;
    motor_command(&state, VOICE_REJECT, 1);
#if VOICE_LEFT_CHANNEL_A
    assert(state.direction_bits==0x40 && state.cr2_left==20000);
    assert(state.cr1_right==(VOICE_MOTOR_FULL_DUTY ? 0 : 7307));
#else
    assert(state.direction_bits==0x01 && state.cr1_right==20000);
    assert(state.cr2_left==(VOICE_MOTOR_FULL_DUTY ? 0 : 7307));
#endif
    motor_command(&state, VOICE_REJECT, 0);
    assert(!state.direction_bits && state.cr2_left==20000 && state.cr1_right==20000);
    voice_result_t best={0}, candidate={0};
    best.decision=VOICE_FORWARD; best.class_id=VOICE_FORWARD; best.probability_q24[0]=16000000;
    candidate.decision=VOICE_STOP; candidate.class_id=VOICE_STOP; candidate.probability_q24[1]=13000000;
    voice_select(&best, &candidate); assert(best.decision==VOICE_STOP);
    candidate.decision=VOICE_BACKWARD; candidate.class_id=VOICE_BACKWARD; candidate.probability_q24[2]=16700000;
    voice_select(&best, &candidate); assert(best.decision==VOICE_STOP);
    assert(voice_round_shift(5,1)==2 && voice_round_shift(7,1)==4);
    assert(voice_round_shift(-5,1)==-2 && voice_round_shift(-7,1)==-4);
    int16_t silent[16000]={0}; int8_t input[VOICE_INPUTS]; voice_result_t result;
    assert(voice_frontend(silent, 15999, 16000u*65536u, 0, input, 0)==-1);
    assert(voice_frontend(silent, 16000, 8000u*65536u, 0, input, 0)==-1);
    assert(voice_frontend(silent, 16000, 16000u*65536u, UINT_MAX, input, 0)==-1);
    assert(!voice_frontend(silent, 16000, 16000u*65536u, 0, input, 0));
    voice_infer(input, &result); assert(result.decision==VOICE_REJECT);
    puts("CONTROL PASS: PWM polarity/channels, disabled drive, direction, reject, stop priority, invalid windows, silence");
    return 0;
}
