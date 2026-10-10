#include "main.h"
#if VOICE_LCD
#include "font.h"
#endif
static uint32_t gpio_output;
#if VOICE_LCD
static int lcd_ready;
#endif
uint32_t c1_cycles(void) { uint32_t value; __asm__ volatile("rdcycle %0":"=r"(value)::"memory"); return value; }
uint32_t c1_gpio_output(void) { return gpio_output; }
static void reverse_audio(int16_t *audio, uint32_t begin, uint32_t end) {
    while (begin<end && begin<--end) {
        int16_t v=audio[begin]; audio[begin++]=audio[end]; audio[end]=v;
    }
}
void c1_align_voice_prefix(int16_t *audio, uint32_t head) {
    if (!head || head>=VOICE_VAD_PRE_SAMPLES) return;
    reverse_audio(audio, 0, head);
    reverse_audio(audio, head, VOICE_VAD_PRE_SAMPLES);
    reverse_audio(audio, 0, VOICE_VAD_PRE_SAMPLES);
}
void c1_disable_lcd(void) {
#if VOICE_LCD
    lcd_ready=0;
#endif
    gpio_output&=~(1u<<2); REG_GPIO_0_DR=gpio_output;
}
int c1_getchar(void) { return (int32_t)REG_UART_0_DATA; }
void c1_puts(const char *value) { while (*value) REG_UART_0_DATA=(uint8_t)*value++; }
void c1_unsigned(uint32_t value) {
    char digits[10]; unsigned n=0;
    do { digits[n++]=(char)('0'+value%10); value/=10; } while (value);
    while (n) REG_UART_0_DATA=(uint8_t)digits[--n];
}
void c1_apply_motor(const motor_state_t *s) {
    /* Active-low CR1=A, CR2=B. Remove duty before changing direction. */
#if VOICE_DRIVE && !VOICE_MOTOR_FULL_DUTY
    uint32_t old_right=gpio_output & (3u<<MOTOR_RIGHT_DIR_SHIFT);
    uint32_t new_right=s->direction_bits & (3u<<MOTOR_RIGHT_DIR_SHIFT);
#endif
    REG_PWM_0_CR1=MOTOR_PWM_MAX; REG_PWM_0_CR2=MOTOR_PWM_MAX;
    gpio_output=(gpio_output & ~MOTOR_DIRECTION_MASK)|s->direction_bits;
    REG_GPIO_0_DR=gpio_output;
    REG_PWM_0_CR2=s->cr2_left;
#if VOICE_DRIVE && !VOICE_MOTOR_FULL_DUTY
    /* The physical right motor starts with DC but stalls at normal duty.
     * Give a starting/reversing motor one pulse, then restore its calibrated
     * duty. Reapplying an unchanged accepted command does not repeat it. */
    if (new_right && new_right!=old_right) {
        REG_PWM_0_CR1=0;
        uint32_t started=c1_cycles();
        while (c1_cycles()-started < MOTOR_RIGHT_STARTUP_MS*(C1_CPU_HZ/1000u)) { }
    }
#endif
    REG_PWM_0_CR1=s->cr1_right;
}
#if VOICE_LCD
static void delay_ms(uint32_t ms) {
    uint32_t start=c1_cycles();
    while (c1_cycles()-start < ms*(C1_CPU_HZ/1000)) { }
}
static int spi(uint8_t value, int data) {
    if (!lcd_ready) return 0;
    if (data) gpio_output|=1u<<2; else gpio_output&=~(1u<<2);
    REG_GPIO_0_DR=gpio_output;
    REG_QSPI_0_LEN=0x80000;
    REG_QSPI_0_TXFIFO=(uint32_t)value<<24;
    REG_QSPI_0_STATUS=258;
    uint32_t start=c1_cycles();
    while ((REG_QSPI_0_STATUS & 0xffff)!=1) {
        if (c1_cycles()-start >= C1_CPU_HZ) { lcd_ready=0; return 0; }
    }
    return 1;
}
static void command(uint8_t c, const uint8_t *data, unsigned n) {
    spi(c, 0); for (unsigned i=0; i<n; ++i) spi(data[i], 1);
}
static void lcd_init(void) {
    /* ECOS embedded-sdk st7735.c sequence; C8 rotation, RGB565, x=2/y=3. */
    static const uint8_t init[]={
        0xb1,3,1,0x2c,0x2d, 0xb2,3,1,0x2c,0x2d,
        0xb3,6,1,0x2c,0x2d,1,0x2c,0x2d, 0xb4,1,7,
        0xc0,3,0xa2,2,0x84, 0xc1,1,0xc5, 0xc2,2,0x0a,0,
        0xc3,2,0x8a,0x2a, 0xc4,2,0x8a,0xee, 0xc5,1,0x0e, 0x36,1,0xc8,
        0xe0,16,0x0f,0x1a,0x0f,0x18,0x2f,0x28,0x20,0x22,0x1f,0x1b,0x23,0x37,0,7,2,0x10,
        0xe1,16,0x0f,0x1b,0x0f,0x17,0x33,0x2c,0x29,0x2e,0x30,0x30,0x39,0x3f,0,7,3,0x10,
        0xf0,1,1, 0xf6,1,0, 0x3a,1,5, 0x29,0};
    lcd_ready=1;
    REG_QSPI_0_STATUS=16; REG_QSPI_0_STATUS=0;
    REG_QSPI_0_INTCFG=0; REG_QSPI_0_DUM=0; REG_QSPI_0_CLKDIV=0;
    delay_ms(120); spi(0x11, 0); delay_ms(120);
    for (unsigned i=0; i<sizeof(init);) { unsigned n=init[i+1]; command(init[i], init+i+2, n); i+=n+2; }
}
#endif
void c1_lcd_line(unsigned line, const char *value, uint16_t color) {
#if VOICE_LCD
    if (!lcd_ready || line>=16) return;
    uint8_t columns[4]={0,2,0,129};
    unsigned y=line*8+3;
    uint8_t rows[4]={0,(uint8_t)y,0,(uint8_t)(y+7)};
    command(0x2a, columns, 4); command(0x2b, rows, 4); spi(0x2c, 0);
    unsigned length=0; while (length<16 && value[length]) ++length;
    for (unsigned r=0; r<8; ++r) for (unsigned x=0; x<128; ++x) {
        unsigned index=x/8;
        unsigned c=index<length ? (unsigned)(uint8_t)value[index] : ' ';
        if (c<32 || c>126) c='.';
        uint16_t pixel=(font8x8[c-32][r] & (128u>>(x%8))) ? color : 0;
        spi((uint8_t)(pixel>>8), 1); spi((uint8_t)pixel, 1);
    }
#else
    (void)line; (void)value; (void)color;
#endif
}
void c1_init(void) {
    REG_UART_0_CLKDIV=C1_CPU_HZ/115200u;
    REG_PWM_0_CTRL=0;
    REG_PWM_0_PSCR=0; REG_PWM_0_CMP=MOTOR_PWM_MAX;
    REG_PWM_0_CR0=MOTOR_PWM_MAX; REG_PWM_0_CR1=MOTOR_PWM_MAX;
    REG_PWM_0_CR2=MOTOR_PWM_MAX; REG_PWM_0_CR3=MOTOR_PWM_MAX;
    gpio_output=0; REG_GPIO_0_DR=0;
    uint32_t outputs=MOTOR_DIRECTION_MASK|(1u<<2)|(1u<<8)|(1u<<9);
    REG_GPIO_0_DDR=(REG_GPIO_0_DDR & ~outputs)|(1u<<VOICE_MIC_SD_GPIO);
    if (VOICE_DRIVE) REG_PWM_0_CTRL=3;
#if VOICE_LCD
    lcd_init();
    for (unsigned line=0; line<16; ++line) c1_lcd_line(line, "", 0xffff);
    c1_lcd_line(0, "C1 VOICE", 0xffff);
    c1_lcd_line(1, VOICE_DRIVE ? "CAR MODE" : "MODEL ONLY", 0x07e0);
#endif
}
