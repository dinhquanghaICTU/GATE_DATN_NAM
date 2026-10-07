#ifndef __ESP_NOW_NODE_H__
#define __ESP_NOW_NODE_H__

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define ESP_NOW_MAC_LEN 6

#pragma pack(push, 1)
typedef struct {
  uint32_t packet_id;
  uint8_t  node_id;
  float    val1;             // Sensor value 1 (e.g., Temp / Distance)
  float    val2;             // Sensor value 2 (e.g., Humidity / Pressure)
  float    battery_volt;     // Battery voltage (V)
  char     fw_version[16];   // Node firmware version
} NodeTelemetry_t;

typedef struct {
  uint8_t  cmd_id;
  uint8_t  node_id;
  uint32_t param;
  char     payload[32];
} NodeCommand_t;
#pragma pack(pop)

typedef void (*esp_now_telemetry_cb_t)(const uint8_t *mac, const NodeTelemetry_t *data, int rssi);
typedef void (*esp_now_raw_recv_cb_t)(const uint8_t *mac, const uint8_t *data, size_t len);

esp_err_t esp_now_node_init(void);
esp_err_t esp_now_node_send(const uint8_t *mac, const uint8_t *data, size_t len);
esp_err_t esp_now_node_send_command(const uint8_t *mac, const NodeCommand_t *cmd);
void esp_now_node_register_telemetry_cb(esp_now_telemetry_cb_t cb);
void esp_now_node_register_raw_cb(esp_now_raw_recv_cb_t cb);
uint32_t esp_now_node_get_total_packets(void);

#endif // __ESP_NOW_NODE_H__
