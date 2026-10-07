#ifndef __LED_H__
#define __LED_H__

#include <stdint.h>
#include <stdbool.h>

typedef enum {
  LED_STATE_OFF = 0,
  LED_STATE_BLE_CONFIG,
  LED_STATE_WIFI_CONNECTING,
  LED_STATE_WIFI_DISCONNECTED,
  LED_STATE_ONLINE_OK,
  LED_STATE_GATE_ACTIVE,
  LED_STATE_OTA_UPDATING,
  LED_STATE_ERROR
} led_state_t;

void led_init(void);
void led_set_state(led_state_t state);
led_state_t led_get_state(void);
void led_notify_activity(void);
void led_task(void *pvParam);

#endif // __LED_H__
