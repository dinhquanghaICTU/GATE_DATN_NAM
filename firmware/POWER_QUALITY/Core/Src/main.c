

#include <stdint.h>
#include <stdio.h>

#include "Hardware/delay/delay.h"
#include "Hardware/led/led.h"
#include "middle/uart_debug/uart_debug.h"
#include "stm32f10x.h"
#include "third_party/ringbuff/ringbuff.h"

int main(void) {
    SystemInit();
    delay_init();
    led_init();

    uart_debug_init(115200);

    delay_ms(100);

    while (1) {
        led_toggle();
        delay_ms(1000);
    }

    return 0;
}
