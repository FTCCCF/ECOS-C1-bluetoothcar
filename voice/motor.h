#ifndef VOICE_MOTOR_H
#define VOICE_MOTOR_H
#include <stdint.h>
#define MOTOR_PWM_MAX 20000
#define MOTOR_SPEED_MAX 6000
#define MOTOR_STD_SPEED 4000
#define MOTOR_LEFT_FORWARD_PERMILLE 952
#define MOTOR_LEFT_REVERSE_PERMILLE 1000
#define MOTOR_RIGHT_STARTUP_MS 150u
#ifndef VOICE_MOTOR_FULL_DUTY
#define VOICE_MOTOR_FULL_DUTY 0
#endif
#ifndef VOICE_LEFT_CHANNEL_A
#define VOICE_LEFT_CHANNEL_A 0
#endif
_Static_assert(VOICE_MOTOR_FULL_DUTY==0 || VOICE_MOTOR_FULL_DUTY==1, "Invalid motor duty profile");
_Static_assert(VOICE_LEFT_CHANNEL_A==0 || VOICE_LEFT_CHANNEL_A==1, "Invalid motor channel mapping");
#define MOTOR_LEFT_DIR_SHIFT (VOICE_LEFT_CHANNEL_A ? 6u : 0u)
#define MOTOR_RIGHT_DIR_SHIFT (VOICE_LEFT_CHANNEL_A ? 0u : 6u)
#define MOTOR_DIRECTION_MASK ((1u<<0)|(1u<<1)|(1u<<6)|(1u<<7))
typedef struct {
    int16_t left, right;
    /* Legacy names denote hardware CR1/A and CR2/B, independent of wiring. */
    uint16_t cr1_right, cr2_left;
    uint16_t direction_bits;
} motor_state_t;
void motor_command(motor_state_t *state, int decision, int drive_enabled);
#endif
