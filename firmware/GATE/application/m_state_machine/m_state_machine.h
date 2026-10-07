#ifndef __M_STATE_MACHINE_H__
#define __M_STATE_MACHINE_H__

#include <stdbool.h>

#define MAX_RETRY_COUNT 5

typedef enum {
  STATE_WIFI_START = 0,
  STATE_WIFI_CONFIG,
  STATE_WIFI_CONNECT,
  STATE_WIFI_GOT_IP,
  STATE_MQTT_CONNECTING,
  STATE_MQTT_CONNECTED,
  STATE_HUNONIC_INIT,
  STATE_RUNNING,
  STATE_WIFI_DISCONNECT,
  STATE_OTA,
  STATE_ERROR
} state_t;

typedef struct {
  state_t state_current;
  state_t state_next;
  bool ble_config_wifi;
  int retry_count;
} m_state_machine_t;

void m_state_machine_init(void);
void m_state_machine_task(void *arg);
void m_state_machine_set_state(state_t state);
state_t m_state_machine_get_state(void);
void m_state_machine_reset_wifi(void);

#endif // __M_STATE_MACHINE_H__
