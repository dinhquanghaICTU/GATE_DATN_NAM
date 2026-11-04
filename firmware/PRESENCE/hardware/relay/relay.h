#ifndef __RELAY_H__
#define __RELAY_H__

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t relay_init(void);
void relay_turn_on(void);
void relay_turn_off(void);
void relay_toggle(void);
void relay_set_state(bool on);
bool relay_is_on(void);

void relay_set_auto_mode(bool enable);
bool relay_get_auto_mode(void);

#ifdef __cplusplus
}
#endif

#endif

