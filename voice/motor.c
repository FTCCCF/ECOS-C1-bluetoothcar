#include "motor.h"
#include "voice.h"
void motor_command(motor_state_t *s, int decision, int drive) {
    if (!drive || decision==VOICE_STOP) s->left=s->right=0;
    else if (decision==VOICE_FORWARD) s->left=s->right=MOTOR_STD_SPEED;
    else if (decision==VOICE_BACKWARD) s->left=s->right=-MOTOR_STD_SPEED;
    /* Rejected speech leaves the last accepted command unchanged. */
    int32_t left=s->left<0 ? -s->left : s->left;
    int32_t right=s->right<0 ? -s->right : s->right;
    if (left>MOTOR_SPEED_MAX) left=MOTOR_SPEED_MAX;
    if (right>MOTOR_SPEED_MAX) right=MOTOR_SPEED_MAX;
    int32_t l_pwm=left*MOTOR_PWM_MAX/MOTOR_SPEED_MAX;
    l_pwm=l_pwm*(s->left<0 ? MOTOR_LEFT_REVERSE_PERMILLE : MOTOR_LEFT_FORWARD_PERMILLE)/1000;
    int32_t r_pwm=right*MOTOR_PWM_MAX/MOTOR_SPEED_MAX;
#if VOICE_MOTOR_FULL_DUTY
    /* Calibrated for the user's 3.3V VM: both motors need full drive. */
    l_pwm=left ? MOTOR_PWM_MAX : 0;
    r_pwm=right ? MOTOR_PWM_MAX : 0;
#endif
#if VOICE_LEFT_CHANNEL_A
    s->cr1_right=(uint16_t)(MOTOR_PWM_MAX-l_pwm);
    s->cr2_left=(uint16_t)(MOTOR_PWM_MAX-r_pwm);
#else
    s->cr2_left=(uint16_t)(MOTOR_PWM_MAX-l_pwm);
    s->cr1_right=(uint16_t)(MOTOR_PWM_MAX-r_pwm);
#endif
    s->direction_bits=(uint16_t)((s->left>0 ? 1u<<MOTOR_LEFT_DIR_SHIFT : s->left<0 ? 2u<<MOTOR_LEFT_DIR_SHIFT : 0) |
                               (s->right>0 ? 1u<<MOTOR_RIGHT_DIR_SHIFT : s->right<0 ? 2u<<MOTOR_RIGHT_DIR_SHIFT : 0));
}
