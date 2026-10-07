#include "m_state_machine.h"
#include "ble.h"
#include "config.h"
#include "esp_log.h"
#include "flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hunonic_bridge.h"
#include "led.h"
#include "mqtt.h"
#include "wifi.h"
#include <string.h>

static const char *TAG = "APP_STATE_MACHINE";

static m_state_machine_t s_fsm = {
    .state_current = STATE_WIFI_START,
    .state_next = STATE_WIFI_START,
    .ble_config_wifi = false,
    .retry_count = 0,
};

static char s_ssid[33] = {0};
static char s_pass[65] = {0};

void m_state_machine_init(void) {
  s_fsm.state_current = STATE_WIFI_START;
  s_fsm.state_next = STATE_WIFI_START;
  s_fsm.retry_count = 0;
  s_fsm.ble_config_wifi = false;
  ESP_LOGI(TAG, "State Machine khoi tao!");
}

void m_state_machine_set_state(state_t state) { s_fsm.state_next = state; }

state_t m_state_machine_get_state(void) { return s_fsm.state_current; }

static void on_ble_wifi_received(const char *ssid, const char *password) {
  ESP_LOGI(TAG, "Nhan cau hinh Wi-Fi tu BLE: SSID=[%s]", ssid);
  strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
  strncpy(s_pass, password, sizeof(s_pass) - 1);
  nvs_save_wifi_credentials(s_ssid, s_pass);
  s_fsm.ble_config_wifi = true;
  m_state_machine_set_state(STATE_WIFI_CONNECT);
}

void m_state_machine_reset_wifi(void) {
  ESP_LOGW(TAG, "Yeu cau xoa cau hinh Wi-Fi -> Chuyen ve BLE CONFIG!");
  nvs_erase_wifi_credentials();
  memset(s_ssid, 0, sizeof(s_ssid));
  memset(s_pass, 0, sizeof(s_pass));
  wifi_disconnect();
  m_state_machine_set_state(STATE_WIFI_CONFIG);
}

void m_state_machine_task(void *arg) {
  ESP_LOGI(TAG, "State Machine Task da khoi chay!");

  while (1) {
    if (s_fsm.state_current != s_fsm.state_next) {
      ESP_LOGI(TAG, "Chuyen trang thai: %d -> %d", s_fsm.state_current,
               s_fsm.state_next);
      s_fsm.state_current = s_fsm.state_next;
    }

    switch (s_fsm.state_current) {
    case STATE_WIFI_START: {
      led_set_state(LED_STATE_WIFI_CONNECTING);
      esp_err_t err = nvs_load_wifi_credentials(s_ssid, sizeof(s_ssid), s_pass,
                                                sizeof(s_pass));
      if (err == ESP_OK && strlen(s_ssid) > 0) {
        ESP_LOGI(TAG, "Doc thay thong tin Wi-Fi tu NVS: SSID=[%s]", s_ssid);
        s_fsm.state_next = STATE_WIFI_CONNECT;
      } else {
#ifdef DEFAULT_WIFI_SSID
        if (strlen(DEFAULT_WIFI_SSID) > 0) {
          ESP_LOGI(TAG, "Su dung Wi-Fi mac dinh tu config: [%s]",
                   DEFAULT_WIFI_SSID);
          strncpy(s_ssid, DEFAULT_WIFI_SSID, sizeof(s_ssid) - 1);
          strncpy(s_pass, DEFAULT_WIFI_PASS, sizeof(s_pass) - 1);
          s_fsm.state_next = STATE_WIFI_CONNECT;
          break;
        }
#endif
        ESP_LOGW(TAG, "Chua co thong tin Wi-Fi trong NVS! Chuyen sang che do "
                      "BLE Config...");
        s_fsm.state_next = STATE_WIFI_CONFIG;
      }
      break;
    }

    case STATE_WIFI_CONFIG: {
      led_set_state(LED_STATE_BLE_CONFIG);
      ESP_LOGI(TAG, "Dang bat BLE provisioning: [%s] ...", BLE_DEVICE_NAME);
      ble_wifi_register_callback(on_ble_wifi_received);
      ble_wifi_init(BLE_DEVICE_NAME);

      while (s_fsm.state_next == STATE_WIFI_CONFIG) {
        vTaskDelay(pdMS_TO_TICKS(500));
      }
      break;
    }

    case STATE_WIFI_CONNECT: {
      led_set_state(LED_STATE_WIFI_CONNECTING);
      if (s_fsm.ble_config_wifi) {
        ble_wifi_stop();
        s_fsm.ble_config_wifi = false;
      }

      app_wifi_config_t cfg = {
          .max_retry = MAX_RETRY_COUNT,
          .retry_delay_ms = 2000,
      };
      strncpy(cfg.ssid, s_ssid, sizeof(cfg.ssid) - 1);
      strncpy(cfg.password, s_pass, sizeof(cfg.password) - 1);

      wifi_connect_sta(&cfg);

      while (s_fsm.state_next == STATE_WIFI_CONNECT) {
        if (wifi_is_connected()) {
          s_fsm.state_next = STATE_WIFI_GOT_IP;
          break;
        }
        if (wifi_get_state() == WIFI_STATE_FAILED) {
          ESP_LOGE(TAG,
                   "Ket noi Wi-Fi that bai, chuyen ve che do BLE Config...");
          s_fsm.state_next = STATE_WIFI_CONFIG;
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
      }
      break;
    }

    case STATE_WIFI_GOT_IP: {
      ESP_LOGI(TAG, "Wi-Fi da san sang! Bat dau ket noi MQTT...");
      app_mqtt_start();
      s_fsm.state_next = STATE_MQTT_CONNECTING;
      break;
    }

    case STATE_MQTT_CONNECTING: {
      led_set_state(LED_STATE_WIFI_CONNECTING);
      int wait_sec = 0;
      while (s_fsm.state_next == STATE_MQTT_CONNECTING && wait_sec < 15) {
        if (app_mqtt_is_connected()) {
          s_fsm.state_next = STATE_MQTT_CONNECTED;
          break;
        }
        if (!wifi_is_connected()) {
          s_fsm.state_next = STATE_WIFI_DISCONNECT;
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        wait_sec++;
      }
      if (s_fsm.state_next == STATE_MQTT_CONNECTING) {
        ESP_LOGW(TAG, "Ket noi MQTT timed out, thu lai sau...");
        vTaskDelay(pdMS_TO_TICKS(2000));
      }
      break;
    }

    case STATE_MQTT_CONNECTED: {
      ESP_LOGI(TAG,
               "MQTT Broker da ket noi! Chuyen sang khoi tao Hunonic Cloud...");
      s_fsm.state_next = STATE_HUNONIC_INIT;
      break;
    }

    case STATE_HUNONIC_INIT: {
      ESP_LOGI(TAG, "DANG KHOI TAO HUNONIC CLOUD INTEGRATION ");
#ifdef HUNONIC_USER_PHONE
      if (strlen(HUNONIC_USER_PHONE) > 0 && strlen(HUNONIC_USER_PASS) > 0) {
        esp_err_t err =
            hunonic_bridge_init(HUNONIC_USER_PHONE, HUNONIC_USER_PASS);
        if (err == ESP_OK) {
          ESP_LOGI(TAG, "Ket noi va dong bo Hunonic Cloud thanh cong!");
        } else {
          ESP_LOGW(TAG,
                   "Hunonic Bridge khoi tao that bai (err=%d). Gateway van "
                   "tiep tuc hoat dong...",
                   err);
        }
      } else {
        ESP_LOGW(TAG, "Chua dien mat khau Hunonic trong secrets.h, bo qua khoi "
                      "tao Hunonic Cloud.");
      }
#endif
      ESP_LOGI(TAG, "E THONG GATEWAY DA ONLINE HOAN TOAN!");
      led_set_state(LED_STATE_ONLINE_OK);
      s_fsm.state_next = STATE_RUNNING;
      break;
    }

    case STATE_RUNNING: {
      if (!wifi_is_connected()) {
        s_fsm.state_next = STATE_WIFI_DISCONNECT;
      }
      vTaskDelay(pdMS_TO_TICKS(500));
      break;
    }

    case STATE_WIFI_DISCONNECT: {
      led_set_state(LED_STATE_WIFI_DISCONNECTED);
      ESP_LOGW(TAG, "Wi-Fi bi ngat ket noi! Cho tu dong ket noi lai...");
      while (s_fsm.state_next == STATE_WIFI_DISCONNECT) {
        if (wifi_is_connected()) {
          s_fsm.state_next = STATE_WIFI_GOT_IP;
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
      break;
    }

    case STATE_OTA: {
      led_set_state(LED_STATE_OTA_UPDATING);
      ESP_LOGW(TAG,
               " [TRANG THAI OTA] He thong dang nap firmware moi qua mang...");
      while (s_fsm.state_next == STATE_OTA) {
        vTaskDelay(pdMS_TO_TICKS(500));
      }
      break;
    }

    case STATE_ERROR: {
      led_set_state(LED_STATE_ERROR);
      vTaskDelay(pdMS_TO_TICKS(1000));
      break;
    }

    default:
      vTaskDelay(pdMS_TO_TICKS(200));
      break;
    }
  }
}
