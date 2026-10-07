#ifndef __FLASH_H__
#define __FLASH_H__

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nvs_save_wifi_credentials(const char *ssid, const char *password);
esp_err_t nvs_load_wifi_credentials(char *ssid, size_t max_ssid_len,
                                    char *password, size_t max_pass_len);
esp_err_t nvs_erase_wifi_credentials(void);

esp_err_t nvs_save_string(const char *key, const char *val);
esp_err_t nvs_load_string(const char *key, char *out_val, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // __FLASH_H__
