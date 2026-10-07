#ifndef __OTA_H__
#define __OTA_H__

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char url[256];
  char version[16];
  char target[32];
  char md5[36];
  int size;
} ota_config_t;

esp_err_t ota_parse_json(const char *json_str, ota_config_t *out_cfg);
esp_err_t ota_start(const ota_config_t *config);
const char *ota_get_current_version(void);
bool ota_is_updating(void);

#ifdef __cplusplus
}
#endif

#endif // __OTA_H__
