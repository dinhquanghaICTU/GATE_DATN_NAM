#ifndef __BLE_H__
#define __BLE_H__

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_SVC_NODE_UUID 0x00FF
#define BLE_CHR_SSID_UUID 0xFF01
#define BLE_CHR_PASS_UUID 0xFF02
#define BLE_CHR_SENSOR_DATA_UUID 0xFF03
#define BLE_CHR_NODE_ID_UUID 0xFF04

#define BLE_MAX_SSID_LEN 33
#define BLE_MAX_PASS_LEN 65
#define BLE_MAX_DATA_LEN 128

typedef void (*ble_wifi_config_cb_t)(const char *ssid, const char *password);

esp_err_t ble_node_init(const char *device_name);

esp_err_t ble_node_update_sensor_data(float temp, float hum, bool presence);

void ble_node_register_wifi_callback(ble_wifi_config_cb_t cb);

bool ble_node_is_connected(void);

esp_err_t ble_node_stop(void);

esp_err_t ble_node_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // __BLE_H__