#ifndef __GATE_CONTROLLER_H__
#define __GATE_CONTROLLER_H__

#include "esp_err.h"
#include <stdbool.h>

esp_err_t gate_controller_init(void);
void gate_controller_task(void *pvParam);

#endif // __GATE_CONTROLLER_H__
