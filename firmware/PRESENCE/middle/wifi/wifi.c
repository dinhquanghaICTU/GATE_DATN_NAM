#include "wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"

static const char *TAG = "MIDDLE_WIFI_ESPNOW";

esp_err_t wifi_esp_now_init(uint8_t channel) {
  esp_err_t err;

  err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Lỗi esp_netif_init: %s", esp_err_to_name(err));
    return err;
  }

  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Lỗi esp_event_loop_create_default: %s", esp_err_to_name(err));
    return err;
  }

  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  if (!sta_netif) {
    ESP_LOGD(TAG, "Default Wi-Fi STA netif already exists or created");
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Lỗi esp_wifi_init: %s", esp_err_to_name(err));
    return err;
  }

  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Lỗi thiết lập Wi-Fi channel %d: %s", channel, esp_err_to_name(err));
    return err;
  }

  esp_wifi_set_ps(WIFI_PS_NONE);

  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

  ESP_LOGI(TAG, "Khởi tạo Wi-Fi Station cho ESP-NOW thành công trên Kênh %d (PS NONE)!", channel);
  return ESP_OK;
}

esp_err_t wifi_esp_now_set_channel(uint8_t channel) {
  return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

