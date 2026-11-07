#include "led.h"

void led_init(void) {
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(LED_GPIO_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin = LED_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_GPIO_PORT, &GPIO_InitStructure);

    /* PC13 is active low on Blue Pill */
    GPIO_SetBits(LED_GPIO_PORT, LED_PIN); /* Turn OFF initially */
}

void led_on(void) {
    GPIO_ResetBits(LED_GPIO_PORT, LED_PIN);
}

void led_off(void) {
    GPIO_SetBits(LED_GPIO_PORT, LED_PIN);
}

void led_toggle(void) {
    LED_GPIO_PORT->ODR ^= LED_PIN;
}
