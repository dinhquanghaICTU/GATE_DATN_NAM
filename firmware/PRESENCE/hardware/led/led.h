#ifndef __LED_H__
#define __LED_H__

#include <stdint.h>
#include <stdbool.h>

typedef enum {
  LED_STATE_OFF = 0,
  LED_STATE_IDLE,             // Cho / Khong co nguoi: Chop nhe bao song
  LED_STATE_PRESENCE_ACTIVE,  // Co nguoi hien dien: Sang lien tuc
  LED_STATE_TRANSMITTING,     // Dang truyen tin
  LED_STATE_ERROR             // Bao loi
} led_state_t;

void led_init(void);
void led_set_state(led_state_t state);
led_state_t led_get_state(void);
void led_notify_activity(void);
void led_task(void *pvParam);

#endif // __LED_H__
