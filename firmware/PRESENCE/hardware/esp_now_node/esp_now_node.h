#ifndef __ESP_NOW_NODE_H__
#define __ESP_NOW_NODE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define ESP_NOW_MAC_LEN 6

#pragma pack(push, 1)
typedef struct {
  uint32_t packet_id;
  uint8_t  node_id;
  float    val1;
  float    val2;
  float    battery_volt;
  uint8_t  presence;
  char     fw_version[15];
} NodeTelemetry_t;

typedef enum {
  NODE_CMD_PING           = 0,
  NODE_CMD_RELAY_OFF      = 1,
  NODE_CMD_RELAY_ON       = 2,
  NODE_CMD_RELAY_TOGGLE   = 3,
  NODE_CMD_SET_AUTO_RELAY = 4,
  NODE_CMD_REQ_TELEMETRY  = 5,
  NODE_CMD_REBOOT         = 10
} node_cmd_id_t;

typedef struct {
  uint8_t  cmd_id;
  uint8_t  node_id;
  uint32_t param;
  char     payload[32];
} NodeCommand_t;
#pragma pack(pop)

typedef void (*esp_now_cmd_cb_t)(const uint8_t *src_mac, const NodeCommand_t *cmd);

esp_err_t esp_now_node_init(void);

esp_err_t esp_now_node_send_telemetry(const uint8_t *gateway_mac, const NodeTelemetry_t *telemetry);

esp_err_t esp_now_node_send_raw(const uint8_t *mac, const uint8_t *data, size_t len);

void esp_now_node_register_cmd_cb(esp_now_cmd_cb_t cb);

uint32_t esp_now_node_get_tx_count(void);

bool esp_now_node_is_connected(void);

#endif

