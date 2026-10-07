#ifndef __RELAY_H__
#define __RELAY_H__

#include <stdbool.h>
#include "esp_err.h"

void relay_init(void);
void relay_turn_on(void);
void relay_turn_off(void);
void relay_toggle(void);
bool relay_is_on(void);

// Support for secondary relay / actuator channel
void relay_set_channel(int ch, bool state);
bool relay_get_channel(int ch);

#endif // __RELAY_H__
