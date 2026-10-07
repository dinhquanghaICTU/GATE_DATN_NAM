#ifndef __BUTTON_H__
#define __BUTTON_H__

#include "esp_err.h"

typedef void (*button_event_cb_t)(int event);

esp_err_t button_init(void);
void button_task(void *pvParam);
void button_task_stop(void);
void button_task_start(void);

#endif // __BUTTON_H__
