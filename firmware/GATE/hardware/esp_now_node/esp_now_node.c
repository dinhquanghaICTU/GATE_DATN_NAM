#include "esp_now_node.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "led.h"
#include <string.h>

static const char *TAG = "HARDWARE_ESP_NOW";

static uint8_t s_broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static esp_now_telemetry_cb_t s_telemetry_cb = NULL;
static esp_now_raw_recv_cb_t s_raw_cb = NULL;
static uint32_t s_total_packets = 0;

static void on_esp_now_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    ESP_LOGD(TAG, "Goi tin ESP-NOW gui khong thanh cong toi MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  }
}

static void on_esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  if (!recv_info || !data || len <= 0) {
    return;
  }

  s_total_packets++;
  led_notify_activity();

  int rssi = -100;
  if (recv_info->rx_ctrl) {
    rssi = recv_info->rx_ctrl->rssi;
  }

  if (len == sizeof(NodeTelemetry_t)) {
    const NodeTelemetry_t *telemetry = (const NodeTelemetry_t *)data;
    ESP_LOGI(TAG, "📡 [ESP-NOW RECV] Node #%d | Packet #%lu | Val1: %.2f | Val2: %.2f | Bat: %.2fV | RSSI: %d dBm",
             telemetry->node_id, (unsigned long)telemetry->packet_id, telemetry->val1, telemetry->val2,
             telemetry->battery_volt, rssi);

    if (s_telemetry_cb) {
      s_telemetry_cb(recv_info->src_addr, telemetry, rssi);
    }
  } else {
    ESP_LOGI(TAG, "📡 [ESP-NOW RAW RECV] Do dai: %d bytes tu MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             len, recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
             recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);

    if (s_raw_cb) {
      s_raw_cb(recv_info->src_addr, data, (size_t)len);
    }
  }
}

esp_err_t esp_now_node_init(void) {
  esp_err_t err = esp_now_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Khoi tao ESP-NOW that bai: %s", esp_err_to_name(err));
    return err;
  }

  esp_now_register_send_cb(on_esp_now_send_cb);
  esp_now_register_recv_cb(on_esp_now_recv_cb);

  // Them peer Broadcast
  esp_now_peer_info_t peer_info = {0};
  memcpy(peer_info.peer_addr, s_broadcast_mac, ESP_NOW_ETH_ALEN);
  peer_info.channel = 0; // Theo channel WiFi hien tai
  peer_info.encrypt = false;

  err = esp_now_add_peer(&peer_info);
  if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
    ESP_LOGE(TAG, "Them broadcast peer that bai: %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Them broadcast peer ESP-NOW thanh cong!");
  }

  return ESP_OK;
}

esp_err_t esp_now_node_send(const uint8_t *mac, const uint8_t *data, size_t len) {
  const uint8_t *target = mac ? mac : s_broadcast_mac;

  // Neu la unicast va chua ton tai trong peer list thi add peer
  if (mac && memcmp(mac, s_broadcast_mac, ESP_NOW_ETH_ALEN) != 0) {
    if (!esp_now_is_peer_exist(mac)) {
      esp_now_peer_info_t peer = {0};
      memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
      peer.channel = 0;
      peer.encrypt = false;
      esp_now_add_peer(&peer);
    }
  }

  return esp_now_send(target, data, len);
}

esp_err_t esp_now_node_send_command(const uint8_t *mac, const NodeCommand_t *cmd) {
  if (!cmd) return ESP_ERR_INVALID_ARG;
  return esp_now_node_send(mac, (const uint8_t *)cmd, sizeof(NodeCommand_t));
}

void esp_now_node_register_telemetry_cb(esp_now_telemetry_cb_t cb) {
  s_telemetry_cb = cb;
}

void esp_now_node_register_raw_cb(esp_now_raw_recv_cb_t cb) {
  s_raw_cb = cb;
}

uint32_t esp_now_node_get_total_packets(void) {
  return s_total_packets;
}
