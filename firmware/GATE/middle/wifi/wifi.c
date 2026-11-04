#include "wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_now_node.h"
#include <string.h>

static const char *TAG = "MIDDLE_WIFI";

static EventGroupHandle_t s_wifi_event_group = NULL;
static wifi_state_t s_current_state = WIFI_STATE_IDLE;
static app_wifi_config_t s_sta_config;
static int s_retry_num = 0;
static bool s_is_reconnect_en = true;
static wifi_event_cb_t s_event_callback = NULL;
static void *s_callback_arg = NULL;

static void wifi_set_state(wifi_state_t state) {
  s_current_state = state;
  if (s_event_callback) {
    s_event_callback(state, s_callback_arg);
  }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
    case WIFI_EVENT_STA_START:
      wifi_set_state(WIFI_STATE_CONNECTING);
      esp_wifi_connect();
      break;

    case WIFI_EVENT_STA_CONNECTED: {
      wifi_event_sta_connected_t *conn = (wifi_event_sta_connected_t *)event_data;
      ESP_LOGI(TAG, "Wi-Fi da bat duoc song AP! Kênh Wi-Fi (Channel): %d. Dang cho cap dia chi IP (DHCP)...", conn ? conn->channel : 0);
      wifi_set_state(WIFI_STATE_CONNECTING);

      esp_wifi_set_ps(WIFI_PS_NONE);
      esp_now_node_init();
      break;
    }

    case WIFI_EVENT_STA_DISCONNECTED: {
      wifi_event_sta_disconnected_t *disconn =
          (wifi_event_sta_disconnected_t *)event_data;
      uint8_t reason = disconn ? disconn->reason : 0;

      xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
      xEventGroupSetBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);

      ESP_LOGW(TAG, "Wi-Fi ngat ket noi! Reason code: %d", reason);
      if (reason == 201) {
        ESP_LOGE(TAG, "-> (Reason 201): KHONG TIM THAY SSID '%s' trong pham vi (Kiem tra router/hotspot co dang bat 2.4GHz khong)!", s_sta_config.ssid);
      } else if (reason == 15 || reason == 202 || reason == 204) {
        ESP_LOGE(TAG, "-> (Reason %d): SAI MAT KHAU Wi-Fi hoac loi xac thuc WPA2/WPA3!", reason);
      } else if (reason == 200) {
        ESP_LOGW(TAG, "-> (Reason 200): Beacon timeout (song Wi-Fi yeu/chap chon)");
      } else if (reason == 203) {
        ESP_LOGE(TAG, "-> (Reason 203): Ket noi bi Router tu choi (Assoc fail)");
      }

      if (!s_is_reconnect_en) {
        wifi_set_state(WIFI_STATE_DISCONNECTED);
        break;
      }

      s_retry_num++;
      wifi_set_state(WIFI_STATE_RECONNECTING);
      ESP_LOGW(TAG, "Tu dong thu ket noi lai Wi-Fi (lan %d)...", s_retry_num);
      vTaskDelay(pdMS_TO_TICKS(2000));
      esp_wifi_connect();
      break;
    }

    default:
      break;
    }
  } else if (event_base == IP_EVENT) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      ESP_LOGI(TAG, "Da ket noi Wi-Fi thanh cong! IP Gateway: " IPSTR,
               IP2STR(&event->ip_info.ip));
      s_retry_num = 0;
      xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
      xEventGroupClearBits(s_wifi_event_group,
                           WIFI_FAIL_BIT | WIFI_DISCONNECTED_BIT);
      wifi_set_state(WIFI_STATE_CONNECTED);

      esp_wifi_set_ps(WIFI_PS_NONE);
      esp_now_node_init();
    }
  }
}

esp_err_t wifi_init(void) {
  if (s_wifi_event_group == NULL) {
    s_wifi_event_group = xEventGroupCreate();
  }

  ESP_ERROR_CHECK(esp_netif_init());
  esp_err_t err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(err);
  }

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
      &instance_got_ip));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

  wifi_country_t country = {
      .cc = "01 ",
      .schan = 1,
      .nchan = 14,
      .max_tx_power = 20,
      .policy = WIFI_COUNTRY_POLICY_AUTO,
  };
  esp_wifi_set_country(&country);

  esp_wifi_set_ps(WIFI_PS_NONE);

  esp_err_t start_err = esp_wifi_start();
  if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_STATE) {
    ESP_LOGE(TAG, "Lỗi esp_wifi_start: %s", esp_err_to_name(start_err));
  }

  ESP_LOGI(TAG, "Wi-Fi Station Mode da khoi tao va START thanh cong (Channels 1-14, No PS)!");
  return ESP_OK;
}

esp_err_t wifi_connect_sta(const app_wifi_config_t *config) {
  if (!config || strlen(config->ssid) == 0) {
    ESP_LOGE(TAG, "Thong tin Wi-Fi khong hop le!");
    return ESP_ERR_INVALID_ARG;
  }

  memcpy(&s_sta_config, config, sizeof(app_wifi_config_t));
  s_retry_num = 0;
  s_is_reconnect_en = true;

  wifi_set_state(WIFI_STATE_CONNECTING);
  if (s_wifi_event_group) {
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT |
                                                 WIFI_FAIL_BIT |
                                                 WIFI_DISCONNECTED_BIT);
  }

  esp_wifi_disconnect();
  vTaskDelay(pdMS_TO_TICKS(150));

  wifi_config_t wifi_config = {0};
  strncpy((char *)wifi_config.sta.ssid, config->ssid,
          sizeof(wifi_config.sta.ssid) - 1);
  strncpy((char *)wifi_config.sta.password, config->password,
          sizeof(wifi_config.sta.password) - 1);
  wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
  wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_LOGI(TAG, "Dang ket noi toi Wi-Fi SSID: [%s]...", config->ssid);

  esp_err_t ret = esp_wifi_start();
  if (ret != ESP_OK && ret != ESP_ERR_WIFI_STATE) {
    ESP_LOGE(TAG, "esp_wifi_start error: %d", ret);
  }

  vTaskDelay(pdMS_TO_TICKS(50));
  esp_err_t conn_err = esp_wifi_connect();
  if (conn_err != ESP_OK) {
    ESP_LOGW(TAG, "esp_wifi_connect() returned: %s (%d)", esp_err_to_name(conn_err), conn_err);
  } else {
    ESP_LOGI(TAG, "Da gui lenh ket noi Wi-Fi thanh cong! Dang quet tim song AP...");
  }
  return ESP_OK;
}

esp_err_t wifi_disconnect(void) {
  s_is_reconnect_en = false;
  return esp_wifi_disconnect();
}

esp_err_t wifi_reconnect(void) {
  s_is_reconnect_en = true;
  wifi_set_state(WIFI_STATE_RECONNECTING);
  return esp_wifi_connect();
}

void wifi_register_callback(wifi_event_cb_t cb, void *user_data) {
  s_event_callback = cb;
  s_callback_arg = user_data;
}

bool wifi_is_connected(void) {
  return (s_current_state == WIFI_STATE_CONNECTED);
}

bool wifi_wait_for_connected(TickType_t timeout_ticks) {
  if (!s_wifi_event_group)
    return false;
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                         pdFALSE, pdTRUE, timeout_ticks);
  return (bits & WIFI_CONNECTED_BIT) != 0;
}

EventGroupHandle_t wifi_get_event_group(void) { return s_wifi_event_group; }

wifi_state_t wifi_get_state(void) { return s_current_state; }

