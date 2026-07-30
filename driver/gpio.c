#include "hal_gpio.h"
#include "hal_gpio_type.h"
#include "board.h"

void gpio_hal_input_enable(uint8_t gpio_id, uint8_t gpio_num){
    REG_GPIO_0_DDR |= (1 << gpio_num);
}

void gpio_hal_output_enable(uint8_t gpio_id, uint8_t gpio_num){
    REG_GPIO_0_DDR &= ~(1 << gpio_num);
}

void gpio_hal_set_level(uint8_t gpio_id, uint8_t gpio_num, uint8_t level){
    if (level == GPIO_LEVEL_HIGH)
        REG_GPIO_0_DR |= (1 << gpio_num);
    else
        REG_GPIO_0_DR &= ~(1 << gpio_num);
}

uint8_t gpio_hal_get_level(uint8_t gpio_id, uint8_t gpio_num){
    return (REG_GPIO_0_DR >> gpio_num) & 1;
}

void gpio_hal_read_update(){}

void gpio_hal_write_update(){}

void gpio_hal_set_fcfg(uint8_t gpio_id, uint8_t gpio_num, uint8_t val){}

void gpio_hal_set_mux(uint8_t gpio_id, uint8_t gpio_num, uint8_t val){}
