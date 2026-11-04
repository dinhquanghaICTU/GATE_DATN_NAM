#include "esp_now_node.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "config.h"
#include "led.h"
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "HARDWARE_ESP_NOW_NODE";

static uint8_t s_broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static esp_now_cmd_cb_t s_cmd_cb = NULL;
static uint32_t s_tx_success_count = 0;
static uint32_t s_tx_fail_count = 0;
static SemaphoreHandle_t s_tx_sem = NULL;
static esp_now_send_status_t s_last_status = ESP_NOW_SEND_FAIL;
static bool s_try_next_channel;
static bool s_is_connected = false;

static void on_esp_now_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
  s_last_status = status;
  if (s_tx_sem) {
    xSemaphoreGive(s_tx_sem);
  }

  if (memcmp(mac_addr, s_broadcast_mac, ESP_NOW_ETH_ALEN) == 0) {
    ESP_LOGD(TAG, "Broadcast sent locally; gateway reception is not confirmed");
    return;
  }
  if (status == ESP_NOW_SEND_SUCCESS) {
    s_tx_success_count++;
    s_is_connected = true;
    led_set_espnow_connected(true);
  } else {
    s_tx_fail_count++;
    s_is_connected = false;
    led_set_espnow_connected(false);
  }
}

static void on_esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  if (!recv_info || !data || len <= 0) {
    return;
  }

  s_is_connected = true;
  led_set_espnow_connected(true);
  led_notify_activity();

  if (len == sizeof(NodeCommand_t)) {
    const NodeCommand_t *cmd = (const NodeCommand_t *)data;
    ESP_LOGI(TAG, "📥 [NHẬN LỆNH GATEWAY] Cmd ID=%d, Node ID=%d, Param=%lu",
             cmd->cmd_id, cmd->node_id, (unsigned long)cmd->param);

    if (s_cmd_cb) {
      s_cmd_cb(recv_info->src_addr, cmd);
    }
  } else {
    ESP_LOGI(TAG, "📥 [NHẬN RAW] %d bytes từ %02X:%02X:%02X:%02X:%02X:%02X",
             len, recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
             recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);
  }
}

esp_err_t esp_now_node_init(void) {
  if (!s_tx_sem) {
    s_tx_sem = xSemaphoreCreateBinary();
    if (!s_tx_sem) return ESP_ERR_NO_MEM;
  }

  esp_err_t err = esp_now_init();
  if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
    ESP_LOGE(TAG, "Khởi tạo esp_now_init thất bại: %s", esp_err_to_name(err));
    return err;
  }

  esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_1M_L);

  esp_now_register_send_cb(on_esp_now_send_cb);
  esp_now_register_recv_cb(on_esp_now_recv_cb);

  if (esp_now_is_peer_exist(s_broadcast_mac)) {
    esp_now_del_peer(s_broadcast_mac);
  }
  esp_now_peer_info_t peer_info = {0};
  memcpy(peer_info.peer_addr, s_broadcast_mac, ESP_NOW_ETH_ALEN);
  peer_info.channel = 0;
  peer_info.ifidx = WIFI_IF_STA;
  peer_info.encrypt = false;
  esp_now_add_peer(&peer_info);

  static const uint8_t gw_mac[ESP_NOW_ETH_ALEN] = GATEWAY_MAC_ADDR;
  if (esp_now_is_peer_exist(gw_mac)) {
    esp_now_del_peer(gw_mac);
  }
  esp_now_peer_info_t gw_peer = {0};
  memcpy(gw_peer.peer_addr, gw_mac, ESP_NOW_ETH_ALEN);
  gw_peer.channel = 0;
  gw_peer.ifidx = WIFI_IF_STA;
  gw_peer.encrypt = false;
  err = esp_now_add_peer(&gw_peer);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Đã thêm Gateway Unicast Peer: %02X:%02X:%02X:%02X:%02X:%02X (Channel current)",
             gw_mac[0], gw_mac[1], gw_mac[2], gw_mac[3], gw_mac[4], gw_mac[5]);
  }

  ESP_LOGI(TAG, "Khởi tạo ESP-NOW Node thành công (PHY Rate: 1 Mbps CCK)!");
  return ESP_OK;
}

esp_err_t esp_now_node_send_raw(const uint8_t *mac, const uint8_t *data, size_t len) {
  if (!data || len == 0) return ESP_ERR_INVALID_ARG;
  const uint8_t *target = mac ? mac : s_broadcast_mac;

  if (!esp_now_is_peer_exist(target)) {
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, target, ESP_NOW_ETH_ALEN);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }

  led_notify_activity();
  esp_err_t err = esp_now_send(target, data, len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Lỗi esp_now_send tới %02X:%02X:%02X:%02X:%02X:%02X: %s",
             target[0], target[1], target[2], target[3], target[4], target[5],
             esp_err_to_name(err));
  }
  return err;
}

esp_err_t esp_now_node_send_telemetry(const uint8_t *gateway_mac, const NodeTelemetry_t *telemetry) {
  if (!telemetry) return ESP_ERR_INVALID_ARG;

  static const uint8_t s_default_gw_mac[ESP_NOW_ETH_ALEN] = GATEWAY_MAC_ADDR;
  const uint8_t *gw_mac = gateway_mac ? gateway_mac : s_default_gw_mac;

  /* Lấy kênh Wi-Fi hiện tại */
  uint8_t cur_channel = 1;
  wifi_second_chan_t sec_chan;
  esp_wifi_get_channel(&cur_channel, &sec_chan);

  /* Chuyển kênh nếu lần trước bị NACK */
  if (s_try_next_channel) {
    wifi_country_t country;
    if (esp_wifi_get_country(&country) == ESP_OK) {
      uint8_t last = country.schan + country.nchan - 1;
      uint8_t next = cur_channel < country.schan || cur_channel >= last ? country.schan : cur_channel + 1;
      if (esp_wifi_set_channel(next, WIFI_SECOND_CHAN_NONE) == ESP_OK) {
        cur_channel = next;
      }
    }
    s_try_next_channel = false;
  }

  /* In log bản tin gửi đi định dạng cột thẳng hàng, emoji rõ ràng */
  const char *presence_str = telemetry->presence ? "🚶 CÓ NGƯỜI" : "🌿 TRỐNG   ";
  if (telemetry->val2 > 150.0f) {
    ESP_LOGI(TAG, "📤 [TX #%-3lu] 🌡️ %5.2f°C | 💨 %7.2f hPa | %s | 🔋 %.2fV | Kênh %u",
             (unsigned long)telemetry->packet_id, telemetry->val1, telemetry->val2,
             presence_str, telemetry->battery_volt, cur_channel);
  } else {
    ESP_LOGI(TAG, "📤 [TX #%-3lu] 🌡️ %5.2f°C | 💧 %5.2f%%     | %s | 🔋 %.2fV | Kênh %u",
             (unsigned long)telemetry->packet_id, telemetry->val1, telemetry->val2,
             presence_str, telemetry->battery_volt, cur_channel);
  }

  if (s_tx_sem) xSemaphoreTake(s_tx_sem, 0);
  esp_err_t err = esp_now_node_send_raw(gw_mac, (const uint8_t *)telemetry, sizeof(*telemetry));
  if (err != ESP_OK) return err;

  if (!s_tx_sem || xSemaphoreTake(s_tx_sem, pdMS_TO_TICKS(500)) != pdTRUE) {
    ESP_LOGW(TAG, "   └─> ⏱️ Timeout: Không nhận được phản hồi ACK từ Gateway");
    return ESP_ERR_TIMEOUT;
  }

  s_try_next_channel = (s_last_status != ESP_NOW_SEND_SUCCESS);
  if (s_try_next_channel) {
    uint8_t probe_next = (cur_channel >= 11) ? 1 : (cur_channel + 1);
    ESP_LOGW(TAG, "   └─> ⚠️ NACK: Gateway chưa nhận -> Lần tới quét Kênh %u", probe_next);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "   └─> ✅ ACK: Gateway đã nhận thành công!");
  return ESP_OK;
}

void esp_now_node_register_cmd_cb(esp_now_cmd_cb_t cb) {
  s_cmd_cb = cb;
}

uint32_t esp_now_node_get_tx_count(void) {
  return s_tx_success_count;
}

bool esp_now_node_is_connected(void) {
  return s_is_connected;
}

