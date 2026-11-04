#ifndef __MIDDLE_WIFI_H__
#define __MIDDLE_WIFI_H__

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_esp_now_init(uint8_t channel);

esp_err_t wifi_esp_now_set_channel(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif

