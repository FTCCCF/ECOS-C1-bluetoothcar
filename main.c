#include "main.h"
#include "font.h"
#include "logo.h"

#define PWM_PSCR            0
#define PWM_CMP             20000
#define MOTOR_PWM_MAX       20000
#define STM32_SPEED_MAX     6000
/* 左轮配平系数（千分率）：前进用 LEFT，后退用 REVERSE；'[' 减 / ']' 加实时调 */
static uint16_t LEFT_ADJUST_PCT = 952;   /* 实车配平实测值（2024 遥控定版） */
static uint16_t REVERSE_ADJUST_PCT = 1000;
#define LEFT_PWM_MIN        0     // 0=关闭左轮最小占空比，两轮同PWM

#define PIN_MOTOR_L_DIR1    GPIO_NUM_0
#define PIN_MOTOR_L_DIR2    GPIO_NUM_1
#define PIN_MOTOR_R_DIR1    GPIO_NUM_6
#define PIN_MOTOR_R_DIR2    GPIO_NUM_7

#define GPIO_ID 0

#define SW_RX_PIN           GPIO_NUM_5  /* 软件串口接收：HC-05 TX 插到 GPIO_5 */
#define SW_BIT_US           104         /* 实测确认 9600bps（位宽~101us） */

#define FB_W 128
#define FB_H 128
#define FB_N (FB_W * FB_H / 2)

static uint32_t fb[FB_N];

int16_t Wheel_Left_Speed = 0;
int16_t Wheel_Right_Speed = 0;
    int16_t Std_Speed = 4000;
static uint8_t mode = 0; // 0=debug, 1=slideshow
static uint8_t slide_idx = 0;

#define turn_rate_num   1
#define turn_rate_den   1   // 未使用（保留）
#define turn_slow_num   5
#define turn_slow_den   10  // 0.5 = 转弯时内侧轮速度系数

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

static st7735_device_t lcd = {
    .dc_gpio_port = 0, .dc_gpio_pin = GPIO_NUM_2,
    .qspi_port = HAL_QSPI_PORT_0, .qspi_cs = HAL_QSPI_CS_0,
    .screen_width = 128, .screen_height = 128,
    .rotation = 0, .horizontal_offset = 2, .vertical_offset = 3,
};

/* 局部刷新：只推 [y0, y0+rows) 行，减少 QSPI 阻塞时间（防吞命令） */
static void lcd_flush_rows(uint16_t y0, uint16_t rows) {
    st7735_addr_set(&lcd, 0, y0, FB_W - 1, y0 + rows - 1);
    uint32_t i = (uint32_t)y0 * (FB_W / 2);
    uint32_t end = i + (uint32_t)rows * (FB_W / 2);
    for (; i < end; i++) st7735_wr_data32(&lcd, fb[i]);
}

static int hp_uart_data_ready(void) {
    return ((REG_UART_1_LSR & 0x080) >> 7) == 0;
}

static const char hexd[] = "0123456789ABCDEF";

/* ---- 软件串口接收（GPIO 位采样，9600 8N1）---- */
static uint8_t sw_rx_level(void) {
    return (uint8_t)((REG_GPIO_0_DR >> SW_RX_PIN) & 1u);
}

static void tim1_delay_us(uint32_t us) {
    REG_TIM_1_CONFIG = 0x0100;      /* stop */
    REG_TIM_1_DATA = us * 72u;      /* 72MHz */
    REG_TIM_1_CONFIG = 0x0101;      /* start, 倒数到 0 */
    while (REG_TIM_1_DATA) { }
}

static void tim1_delay_ticks(uint32_t ticks) {
    REG_TIM_1_CONFIG = 0x0100;
    REG_TIM_1_DATA = ticks;
    REG_TIM_1_CONFIG = 0x0101;
    while (REG_TIM_1_DATA) { }
}

static uint32_t sw_bit_ticks = 104u * 72u;   /* 保留：未用 */
/* 静默解码版：绝对时间轴调度，消除逐位累计漂移 */
static int sw_uart_try_read(uint8_t *out) {
    uint32_t guard = 1600;                   /* ~20ms 空闲窗口：给调试打印留时隙 */
    while (sw_rx_level()) {
        if (--guard == 0) return 0;
    }
    /* 检测到下降沿。启动自由计数的 TIM_1，按绝对时刻采样：
     * 位中心 k 的时刻 = 检测点 + (k+1.5)*位宽；读取耗时不再引入累计误差 */
    REG_TIM_1_CONFIG = 0x0100;
    REG_TIM_1_DATA = 0xFFFFFF;
    REG_TIM_1_CONFIG = 0x0101;

    uint32_t step = SW_BIT_US * 72u;         /* 位宽 ticks */
    uint32_t target = 0xFFFFFF - step * 3u / 2u;   /* b0 中心 */

    *out = 0;
    for (int b = 0; b < 8; b++) {
        while (REG_TIM_1_DATA > target) { }  /* 等到该位中心 */
        uint8_t v = sw_rx_level();
        *out = (uint8_t)((*out >> 1) | (v << 7));   /* LSB first */
        target -= step;
    }
    /* 不等/不验停止位：立即返回，留足余量抓背靠背下一帧的起始沿。
     * 噪声假字节由主循环 default 分支过滤 */
    return 1;
}

static void motor_apply(void) {
    int16_t l_pwm, r_pwm;

    int16_t adj = (Wheel_Left_Speed < 0) ? (int16_t)REVERSE_ADJUST_PCT
                                         : (int16_t)LEFT_ADJUST_PCT;
    l_pwm = (int16_t)((int32_t)speed_to_pwm(Wheel_Left_Speed) * adj / 1000);
    if (Wheel_Left_Speed != 0 && l_pwm < LEFT_PWM_MIN)
        l_pwm = LEFT_PWM_MIN;
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

    pwm_hal_set_compare(NULL, 0, PWM_CH2, MOTOR_PWM_MAX - l_pwm);
    pwm_hal_set_compare(NULL, 0, PWM_CH1, MOTOR_PWM_MAX - r_pwm);
}

/* 调试页：全帧绘制（页切换时用） */
static void draw_debug_full(void) {
    fb_clear(0x0000);
    fb_str(0, 0,  "L-SPD:", 0xFFFF);
    fb_dec(56, 0, Wheel_Left_Speed, 0xFFFF);
    fb_str(0, 8,  "R-SPD:", 0xFFFF);
    fb_dec(56, 8, Wheel_Right_Speed, 0xFFFF);
    fb_str(0, 16, "PWM-L:", 0xFFFF);
    fb_dec(56, 16, speed_to_pwm(Wheel_Left_Speed), 0xFFFF);
    fb_str(0, 24, "PWM-R:", 0xFFFF);
    fb_dec(56, 24, speed_to_pwm(Wheel_Right_Speed), 0xFFFF);
    fb_str(0, 32, "SPD=", 0xFFFF);
    fb_dec(40, 32, Std_Speed, 0xFFFF);
    fb_str(0, 48, "TRIM=", 0xFFFF);
    fb_dec(40, 48, (int16_t)LEFT_ADJUST_PCT, 0x07E0);
    st7735_fill_img(&lcd, 0, 0, FB_W, FB_H, fb);
}

/* 调试页数值更新：只推顶部 56 行 */
static void draw_debug_vals(void) {
    fb_clear(0x0000);
    fb_str(0, 0,  "L-SPD:", 0xFFFF);
    fb_dec(56, 0, Wheel_Left_Speed, 0xFFFF);
    fb_str(0, 8,  "R-SPD:", 0xFFFF);
    fb_dec(56, 8, Wheel_Right_Speed, 0xFFFF);
    fb_str(0, 16, "PWM-L:", 0xFFFF);
    fb_dec(56, 16, speed_to_pwm(Wheel_Left_Speed), 0xFFFF);
    fb_str(0, 24, "PWM-R:", 0xFFFF);
    fb_dec(56, 24, speed_to_pwm(Wheel_Right_Speed), 0xFFFF);
    fb_str(0, 32, "SPD=", 0xFFFF);
    fb_dec(40, 32, Std_Speed, 0xFFFF);
    fb_str(0, 48, "TRIM=", 0xFFFF);
    fb_dec(40, 48, (int16_t)LEFT_ADJUST_PCT, 0x07E0);
    lcd_flush_rows(0, 56);
}

void main(void) {
    char cmd;
    uint32_t idle_cnt = 0;
    uint8_t rb;

    hal_sys_uart_init();
    hal_sys_putstr("RC MODE: SW-UART RX @9600 GPIO5\n");

    hal_qspi_config_t qc = {.clkdiv = 0};
    hal_qspi_init(HAL_QSPI_PORT_0, &qc);

    st7735_init(&lcd);
    fb_clear(0x0000);

    /* 方向脚全 LOW（上电静止） */
    gpio_hal_output_enable(GPIO_ID, PIN_MOTOR_L_DIR1);
    gpio_hal_output_enable(GPIO_ID, PIN_MOTOR_L_DIR2);
    gpio_hal_output_enable(GPIO_ID, PIN_MOTOR_R_DIR1);
    gpio_hal_output_enable(GPIO_ID, PIN_MOTOR_R_DIR2);
    gpio_hal_set_level(GPIO_ID, PIN_MOTOR_L_DIR1, GPIO_LEVEL_LOW);
    gpio_hal_set_level(GPIO_ID, PIN_MOTOR_L_DIR2, GPIO_LEVEL_LOW);
    gpio_hal_set_level(GPIO_ID, PIN_MOTOR_R_DIR1, GPIO_LEVEL_LOW);
    gpio_hal_set_level(GPIO_ID, PIN_MOTOR_R_DIR2, GPIO_LEVEL_LOW);

    /* 软件串口接收脚：输入 + 上拉（空闲稳定为高） */
    gpio_hal_input_enable(GPIO_ID, SW_RX_PIN);
    REG_GPIO_0_PUB |= (1u << SW_RX_PIN);

    /* GPIO_5 软件串口接收：输入 + 上拉（空闲稳定为高） */
    gpio_hal_input_enable(GPIO_ID, SW_RX_PIN);
    REG_GPIO_0_PUB |= (1u << SW_RX_PIN);

    pwm_config_t pcfg = { .pscr = PWM_PSCR, .cmp = PWM_CMP };
    pwm_hal_init(NULL, 0, &pcfg);
    pwm_hal_set_compare(NULL, 0, PWM_CH2, MOTOR_PWM_MAX);
    pwm_hal_set_compare(NULL, 0, PWM_CH1, MOTOR_PWM_MAX);
    pwm_hal_enable(NULL, 0);

    hal_hp_uart_init(9600);   /* 仅初始化；硬件RX已损坏，发送方向未用 */

    /* ---- 遥控主循环：GPIO 软件串口收命令（含WS背靠背优化）----
     * 调试回显走缓冲：字节先入队，线路空闲 20ms 时统一打印，
     * 避免打印阻塞挤掉背靠背帧的起始沿 */
    static char dbg_buf[64];
    static uint8_t dbg_len = 0;
    static uint16_t slide_idle = 0;          /* 轮播翻页计时（×20ms） */

    fb_str(0, 0, "RC Ready", 0xFFFF);
    st7735_fill_img(&lcd, 0, 0, FB_W, FB_H, fb);

    for (;;) {
        if (sw_uart_try_read(&rb)) {
            cmd = (char)rb;
            /* 入调试缓冲（原样保存，空闲时格式化打印） */
            if (dbg_len < sizeof(dbg_buf)) dbg_buf[dbg_len++] = rb;

            switch (cmd) {
            case 'W': case 'w':
                Wheel_Left_Speed = Std_Speed;
                Wheel_Right_Speed = Std_Speed;
                break;
            case 'S': case 's':
                Wheel_Left_Speed = 0;
                Wheel_Right_Speed = 0;
                break;
            case 'D': case 'd':
                Wheel_Left_Speed = Std_Speed;
                Wheel_Right_Speed = Std_Speed * turn_slow_num / turn_slow_den;
                break;
            case 'L': case 'l':
            case 'A': case 'a':
                Wheel_Left_Speed = Std_Speed * turn_slow_num / turn_slow_den;
                Wheel_Right_Speed = Std_Speed;
                break;
            case 'X': case 'x':
                Wheel_Left_Speed = -Std_Speed;
                Wheel_Right_Speed = -Std_Speed;
                break;
            case '+':
                Std_Speed += 200;
                if (Std_Speed > STM32_SPEED_MAX) Std_Speed = STM32_SPEED_MAX;
                break;
            case '-':
                Std_Speed -= 200;
                if (Std_Speed < 200) Std_Speed = 200;
                break;
            case 'J': case 'j':             /* 左轮配平 -1%：修“微左拐” */
                if (LEFT_ADJUST_PCT > 500) LEFT_ADJUST_PCT -= 10;
                break;
            case 'I': case 'i':             /* 左轮配平 +1%：修“微右拐” */
                if (LEFT_ADJUST_PCT < 1200) LEFT_ADJUST_PCT += 10;
                break;
            case 'P': case 'p':
                mode = !mode;
                slide_idx = 0;
                if (mode) {
                    memcpy(fb, ysyx_logo, sizeof(fb));
                    st7735_fill_img(&lcd, 0, 0, FB_W, FB_H, fb);
                } else {
                    draw_debug_full();
                }
                break;
            default:
                continue;               /* 未识别字节：不动作 */
            }
            motor_apply();              /* 立即执行 */
            if (!mode) draw_debug_vals();   /* 调试页数值局部刷新 */
        } else {
            /* 20ms 线路空闲：安全打印缓冲的蓝牙数据 */
            if (dbg_len) {
                for (uint8_t i = 0; i < dbg_len; i++) {
                    char c = dbg_buf[i];
                    if (c >= 32 && c < 127) {
                        hal_sys_putchar(c);
                    } else {
                        hal_sys_putchar('[');
                        hal_sys_putchar(hexd[((uint8_t)c >> 4) & 0xF]);
                        hal_sys_putchar(hexd[(uint8_t)c & 0xF]);
                        hal_sys_putchar(']');
                    }
                }
                hal_sys_putstr("\n");
                dbg_len = 0;
            }
            /* 轮播模式：每 ~3s 在 ACSL ↔ YSYX 间翻页 */
            if (mode && ++slide_idle >= 150) {
                slide_idle = 0;
                slide_idx = !slide_idx;
                memcpy(fb, slide_idx ? asc_logo : ysyx_logo, sizeof(fb));
                st7735_fill_img(&lcd, 0, 0, FB_W, FB_H, fb);
            }
        }
    }
}
