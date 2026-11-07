#ifndef __LED_H__
#define __LED_H__

#include <stdint.h>
#include "stm32f10x.h"

#define LED_PIN         GPIO_Pin_13
#define LED_GPIO_PORT   GPIOC
#define LED_GPIO_CLK    RCC_APB2Periph_GPIOC

void led_init(void);
void led_on(void);
void led_off(void);
void led_toggle(void);

#endif /* __LED_H__ */
