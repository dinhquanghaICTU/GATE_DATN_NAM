#ifndef __PRESENCE_CONTROLLER_H__
#define __PRESENCE_CONTROLLER_H__

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t presence_controller_init(void);

void presence_controller_start(void);

void presence_controller_trigger_telemetry(void);

void presence_controller_get_sensor_cache(float *temp, float *hum, bool *presence);

#ifdef __cplusplus
}
#endif

#endif

