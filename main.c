#include "main.h"
#include "font.h"
#include "logo.h"

#define PWM_PSCR            71
#define PWM_CMP             20000
#define MOTOR_PWM_MAX       20000
#define STM32_SPEED_MAX     4800
#define LEFT_ADJUST_PCT     30

#define PIN_MOTOR_L_DIR1    GPIO_NUM_0
#define PIN_MOTOR_L_DIR2    GPIO_NUM_1
#define PIN_MOTOR_R_DIR1    GPIO_NUM_6
#define PIN_MOTOR_R_DIR2    GPIO_NUM_7

#define GPIO_ID 0

#define FB_W 128
#define FB_H 128
#define FB_N (FB_W * FB_H / 2)

static uint32_t fb[FB_N];

int16_t Wheel_Left_Speed = 0;
int16_t Wheel_Right_Speed = 0;
    int16_t Std_Speed = 650;
static uint8_t mode = 0; // 0=debug, 1=slideshow
static uint8_t slide_idx = 0;

#define turn_rate_num   14
#define turn_rate_den   10  // 3/5 = 0.6
#define left_cmps_num   5
#define left_cmps_den   10  // 6/5 = 1.2

static void fb_clear(uint16_t bg){
    uint32_t v = ((uint32_t)bg << 16) | bg;
    for (int i = 0; i < FB_N; i++) fb[i] = v;
}

static void fb_pixel(int x, int y, uint16_t color){
    if (x < 0 || x >= FB_W || y < 0 || y >= FB_H) return;
    int i = y * (FB_W / 2) + x / 2;
    if (x & 1)
        fb[i] = (fb[i] & 0xFFFF0000) | color;
    else
        fb[i] = (fb[i] & 0x0000FFFF) | ((uint32_t)color << 16);
}

static void fb_char(int x, int y, char c, uint16_t fg){
    if (c < 32 || c > 126) c = '.';
    const uint8_t *bm = font8x8[c - 32];
    for (int r = 0; r < 8; r++)
        for (int c2 = 0; c2 < 8; c2++)
            if (bm[r] & (0x80 >> c2))
                fb_pixel(x + c2, y + r, fg);
}

static void fb_str(int x, int y, const char *s, uint16_t fg){
    while (*s) {
        fb_char(x, y, *s++, fg);
        x += 8;
        if (x > FB_W - 8) { x = 0; y += 8; }
    }
}

static void fb_dec(int x, int y, int16_t val, uint16_t fg){
    char buf[8];
    uint8_t i = 0, neg = 0, j;
    if (val < 0) { neg = 1; val = -val; }
    if (val == 0) { fb_char(x, y, '0', fg); return; }
    while (val > 0 && i < sizeof(buf)-1) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    if (neg) fb_char(x, y, '-', fg);
    if (neg) x += 8;
    for (j = i; j > 0; j--)
        fb_char(x + (i-j)*8, y, buf[j-1], fg);
}

static void put_dec(int16_t val) {
    char buf[8];
    uint8_t i = 0, neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    if (val == 0) { hal_sys_putchar('0'); return; }
    while (val > 0 && i < sizeof(buf)-1) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    if (neg) hal_sys_putchar('-');
    while (i > 0) hal_sys_putchar(buf[--i]);
}

static int16_t speed_to_pwm(int16_t spd) {
    if (spd < 0) spd = -spd;
    if (spd > STM32_SPEED_MAX) spd = STM32_SPEED_MAX;
    return (int16_t)((int32_t)spd * MOTOR_PWM_MAX / STM32_SPEED_MAX);
}

static int hp_uart_data_ready(void) {
    return ((REG_UART_1_LSR & 0x080) >> 7) == 0;
}

static void motor_apply(void) {
    int16_t l_pwm, r_pwm;

    l_pwm = (int16_t)((int32_t)speed_to_pwm(Wheel_Left_Speed) * LEFT_ADJUST_PCT / 100);
    r_pwm = speed_to_pwm(Wheel_Right_Speed);

    if (Wheel_Left_Speed > 0) {
        gpio_hal_set_level(GPIO_ID, PIN_MOTOR_L_DIR1, GPIO_LEVEL_HIGH);
        gpio_hal_set_level(GPIO_ID, PIN_MOTOR_L_DIR2, GPIO_LEVEL_LOW);
    } else if (Wheel_Left_Speed < 0) {
        gpio_hal_set_level(GPIO_ID, PIN_MOTOR_L_DIR1, GPIO_LEVEL_LOW);
        gpio_hal_set_level(GPIO_ID, PIN_MOTOR_L_DIR2, GPIO_LEVEL_HIGH);
    } else {
        gpio_hal_set_level(GPIO_ID, PIN_MOTOR_L_DIR1, GPIO_LEVEL_LOW);
        gpio_hal_set_level(GPIO_ID, PIN_MOTOR_L_DIR2, GPIO_LEVEL_LOW);
    }

    if (Wheel_Right_Speed > 0) {
        gpio_hal_set_level(GPIO_ID, PIN_MOTOR_R_DIR1, GPIO_LEVEL_HIGH);
        gpio_hal_set_level(GPIO_ID, PIN_MOTOR_R_DIR2, GPIO_LEVEL_LOW);
    } else if (Wheel_Right_Speed < 0) {
        gpio_hal_set_level(GPIO_ID, PIN_MOTOR_R_DIR1, GPIO_LEVEL_LOW);
        gpio_hal_set_level(GPIO_ID, PIN_MOTOR_R_DIR2, GPIO_LEVEL_HIGH);
    } else {
        gpio_hal_set_level(GPIO_ID, PIN_MOTOR_R_DIR1, GPIO_LEVEL_LOW);
        gpio_hal_set_level(GPIO_ID, PIN_MOTOR_R_DIR2, GPIO_LEVEL_LOW);
    }

    pwm_hal_set_compare(NULL, 0, PWM_CH1, l_pwm);
    pwm_hal_set_compare(NULL, 0, PWM_CH2, r_pwm);
}

void main(void) {
    char cmd;
    uint32_t loop_cnt = 0;

    hal_sys_uart_init();
    hal_sys_putstr("C1 Bluetooth Car Boot OK\n");

    hal_qspi_config_t qc = {.clkdiv = 0};
    hal_qspi_init(HAL_QSPI_PORT_0, &qc);

    st7735_device_t lcd = {
        .dc_gpio_port = 0, .dc_gpio_pin = GPIO_NUM_2,
        .qspi_port = HAL_QSPI_PORT_0, .qspi_cs = HAL_QSPI_CS_0,
        .screen_width = 128, .screen_height = 128,
        .rotation = 0, .horizontal_offset = 2, .vertical_offset = 3,
    };
    st7735_init(&lcd);
    fb_clear(0x0000);
    st7735_fill_img(&lcd, 0, 0, FB_W, FB_H, fb);

    gpio_hal_output_enable(GPIO_ID, PIN_MOTOR_L_DIR1);
    gpio_hal_output_enable(GPIO_ID, PIN_MOTOR_L_DIR2);
    gpio_hal_output_enable(GPIO_ID, PIN_MOTOR_R_DIR1);
    gpio_hal_output_enable(GPIO_ID, PIN_MOTOR_R_DIR2);

    gpio_hal_set_level(GPIO_ID, PIN_MOTOR_L_DIR1, GPIO_LEVEL_LOW);
    gpio_hal_set_level(GPIO_ID, PIN_MOTOR_L_DIR2, GPIO_LEVEL_LOW);
    gpio_hal_set_level(GPIO_ID, PIN_MOTOR_R_DIR1, GPIO_LEVEL_LOW);
    gpio_hal_set_level(GPIO_ID, PIN_MOTOR_R_DIR2, GPIO_LEVEL_LOW);

    pwm_config_t pcfg = { .pscr = PWM_PSCR, .cmp = PWM_CMP };
    pwm_hal_init(NULL, 0, &pcfg);
    pwm_hal_set_compare(NULL, 0, PWM_CH1, 0);
    pwm_hal_set_compare(NULL, 0, PWM_CH2, 0);
    pwm_hal_enable(NULL, 0);

    hal_hp_uart_init(38400);
    hal_sys_putstr("Bluetooth ready at 38400\n");

    fb_str(0, 0, "Car Ready", 0xFFFF);
    st7735_fill_img(&lcd, 0, 0, FB_W, FB_H, fb);

    while (1) {
        if (hp_uart_data_ready()) {
            hal_hp_uart_recv(&cmd);

            switch (cmd) {
            case 'W': case 'w':
                Wheel_Left_Speed = Std_Speed * left_cmps_num / left_cmps_den;
                Wheel_Right_Speed = Std_Speed;
                break;
            case 'S': case 's':
                Wheel_Left_Speed = 0;
                Wheel_Right_Speed = 0;
                break;
            case 'D': case 'd':
                Wheel_Left_Speed = Std_Speed * turn_rate_num / turn_rate_den;
                Wheel_Right_Speed = Std_Speed;
                break;
            case 'A': case 'a':
                Wheel_Left_Speed = Std_Speed * left_cmps_num / left_cmps_den;
                Wheel_Right_Speed = Std_Speed * turn_rate_num / turn_rate_den;
                break;
            case 'X': case 'x':
                Wheel_Left_Speed = -Std_Speed;
                Wheel_Right_Speed = -Std_Speed;
                break;
            case '+':
                Std_Speed += 200;
                if (Std_Speed > STM32_SPEED_MAX) Std_Speed = STM32_SPEED_MAX;
                hal_sys_putstr("SPD+ ");
                break;
            case '-':
                Std_Speed -= 200;
                if (Std_Speed < 200) Std_Speed = 200;
                hal_sys_putstr("SPD- ");
                break;
            case 'P': case 'p':
                mode = !mode;
                slide_idx = 0;
                hal_sys_putstr("MODE ");
                break;
            }
        }

        motor_apply();

        if (++loop_cnt >= 20) {
            loop_cnt = 0;
            hal_sys_putstr("[DBG] L=");
            put_dec(Wheel_Left_Speed);
            hal_sys_putstr(" R=");
            put_dec(Wheel_Right_Speed);
            hal_sys_putstr(" | PWM_L=");
            put_dec(speed_to_pwm(Wheel_Left_Speed));
            hal_sys_putstr(" PWM_R=");
            put_dec(speed_to_pwm(Wheel_Right_Speed));
            hal_sys_putstr(" | SPD=");
            put_dec(Std_Speed);
            hal_sys_putstr("\n");

            if (mode) {
                if (slide_idx)
                    memcpy(fb, asc_logo, sizeof(fb));
                else
                    memcpy(fb, ysyx_logo, sizeof(fb));
                slide_idx = !slide_idx;
            } else {
                fb_clear(0x0000);
                fb_str(0, 0, "L-SPD:", 0xFFFF);
                fb_dec(56, 0, Wheel_Left_Speed, 0xFFFF);
                fb_str(0, 10, "R-SPD:", 0xFFFF);
                fb_dec(56, 10, Wheel_Right_Speed, 0xFFFF);
                fb_str(0, 20, "PWM-L:", 0xFFFF);
                fb_dec(56, 20, speed_to_pwm(Wheel_Left_Speed), 0xFFFF);
                fb_str(0, 30, "PWM-R:", 0xFFFF);
                fb_dec(56, 30, speed_to_pwm(Wheel_Right_Speed), 0xFFFF);
                fb_str(0, 40, "SPD:", 0xFFFF);
                fb_dec(56, 40, Std_Speed, 0xFFFF);
            }
            st7735_fill_img(&lcd, 0, 0, FB_W, FB_H, fb);
        }

        hal_delay_ms(0, 100);
    }
}
