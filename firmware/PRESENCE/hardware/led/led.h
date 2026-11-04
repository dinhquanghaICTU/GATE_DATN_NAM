#ifndef __LED_H__
#define __LED_H__

#include <stdint.h>
#include <stdbool.h>

typedef enum {
  LED_STATE_OFF = 0,
  LED_STATE_CONNECTED,       // Đã kết nối Gateway ESP-NOW -> Giữ sáng liên tục (Solid ON)
  LED_STATE_DISCONNECTED,    // Chưa kết nối / mất kết nối -> Chớp nháy nhanh (Fast blink)
  LED_STATE_IDLE,
  LED_STATE_PRESENCE_ACTIVE,
  LED_STATE_TRANSMITTING,
  LED_STATE_ERROR
} led_state_t;

void led_init(void);
void led_set_state(led_state_t state);
void led_set_espnow_connected(bool connected);
led_state_t led_get_state(void);
void led_notify_activity(void);
void led_task(void *pvParam);

#endif

