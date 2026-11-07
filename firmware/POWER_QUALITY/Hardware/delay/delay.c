/**
 ******************************************************************************
 * @file    delay.c
 * @brief   Hàm tạo trễ chính xác dùng SysTick Timer (Bare-metal STM32F103)
 ******************************************************************************
 */

#include "delay.h"

static uint8_t  s_fac_us = 0;
static uint16_t s_fac_ms = 0;

void delay_init(void) {
    /* Chọn nguồn xung nhịp cho SysTick là HCLK / 8 (72 MHz / 8 = 9 MHz) */
    SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK_Div8);
    s_fac_us = SystemCoreClock / 8000000;    /* 9 xung nhịp = 1 us */
    s_fac_ms = (uint16_t)s_fac_us * 1000;    /* 9000 xung nhịp = 1 ms */
}

void delay_us(uint32_t us) {
    uint32_t temp;
    SysTick->LOAD = us * s_fac_us;
    SysTick->VAL = 0x00;
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
    do {
        temp = SysTick->CTRL;
    } while ((temp & 0x01) && !(temp & (1 << 16)));
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    SysTick->VAL = 0x00;
}

void delay_ms(uint32_t ms) {
    while (ms > 500) {
        delay_us(500000);
        ms -= 500;
    }
    delay_us(ms * 1000);
}
