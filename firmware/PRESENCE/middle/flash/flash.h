#ifndef __FLASH_H__
#define __FLASH_H__

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nvs_save_node_id(uint8_t id);
esp_err_t nvs_load_node_id(uint8_t *id);

esp_err_t nvs_save_interval(uint32_t interval_ms);
esp_err_t nvs_load_interval(uint32_t *interval_ms);

#ifdef __cplusplus
}
#endif

#endif // __FLASH_H__
