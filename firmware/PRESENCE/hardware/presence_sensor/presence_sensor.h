#ifndef __PRESENCE_SENSOR_H__
#define __PRESENCE_SENSOR_H__

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "ld2410_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*presence_state_cb_t)(bool is_detected);

typedef struct {
  ld2410_report_t radar;
  bool online;
  bool detected; /* Presence after hold-off; preserved while UART is offline. */
  uint32_t frame_count;
  uint32_t age_ms;
} presence_sensor_data_t;

/* Returns false until the first valid UART report. */
bool presence_sensor_get_data(presence_sensor_data_t *out);

esp_err_t presence_sensor_init(void);

void presence_sensor_register_callback(presence_state_cb_t cb);

bool presence_sensor_is_detected(void);

uint32_t presence_sensor_get_active_duration_sec(void);

void presence_sensor_set_hold_off_ms(uint32_t ms);

uint32_t presence_sensor_get_hold_off_ms(void);

#ifdef __cplusplus
}
#endif

#endif

