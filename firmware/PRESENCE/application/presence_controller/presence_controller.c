#include "presence_controller.h"
#include "config.h"
#include "bmp280.h"
#include "SHT30.h"
#include "led.h"
#include "presence_sensor.h"
#include "wifi.h"
#include "esp_now_node.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "APP_PRESENCE_CTRL";

typedef enum {
  ACTIVE_SENSOR_NONE = 0,
  ACTIVE_SENSOR_BMP280,
  ACTIVE_SENSOR_BME280,
  ACTIVE_SENSOR_SHT30,
} active_sensor_t;

static active_sensor_t s_sensor_type = ACTIVE_SENSOR_NONE;
static float s_cached_temp = 28.0f;
static float s_cached_press = 1013.25f;
static float s_cached_hum = 65.0f;
static bool s_cached_presence = false;
static uint32_t s_packet_counter = 1;
static SemaphoreHandle_t s_trigger_sem = NULL;

static void send_telemetry_packet(void) {
  float temp = 0.0f;
  float press = 0.0f;
  float hum = 0.0f;
  esp_err_t err = ESP_FAIL;

  static uint32_t retry_sensor_counter = 0;

  if (s_sensor_type == ACTIVE_SENSOR_BMP280 || s_sensor_type == ACTIVE_SENSOR_BME280) {
    err = bmp280_read_data(&temp, &press, &hum);
    if (err == ESP_OK) {
      s_cached_temp = temp;
      s_cached_press = press;
      if (bmp280_is_bme280()) {
        s_cached_hum = hum;
      } else {
        s_cached_hum = press;
      }
    }
  } else if (s_sensor_type == ACTIVE_SENSOR_SHT30) {
    err = sht30_read_data(&temp, &hum);
    if (err == ESP_OK) {
      s_cached_temp = temp;
      s_cached_hum = hum;
    }
  } else {

    if (++retry_sensor_counter >= 10) {
      retry_sensor_counter = 0;
      if (bmp280_init() == ESP_OK) {
        s_sensor_type = bmp280_is_bme280() ? ACTIVE_SENSOR_BME280 : ACTIVE_SENSOR_BMP280;
        if (bmp280_read_data(&temp, &press, &hum) == ESP_OK) {
          s_cached_temp = temp;
          s_cached_press = press;
          s_cached_hum = bmp280_is_bme280() ? hum : press;
          err = ESP_OK;
        }
      } else if (sht30_init() == ESP_OK) {
        s_sensor_type = ACTIVE_SENSOR_SHT30;
        if (sht30_read_data(&temp, &hum) == ESP_OK) {
          s_cached_temp = temp;
          s_cached_hum = hum;
          err = ESP_OK;
        }
      }
    }
  }

  if (err != ESP_OK) {
    ESP_LOGD(TAG, "Không đọc được cảm biến (BMP280/SHT30), dùng dữ liệu cache cũ (%.1f°C, %.1f)",
             s_cached_temp, s_cached_hum);
  }

  s_cached_presence = presence_sensor_is_detected();

  NodeTelemetry_t telemetry;
  memset(&telemetry, 0, sizeof(telemetry));
  telemetry.packet_id = s_packet_counter++;
  telemetry.node_id = NODE_ID;
  telemetry.val1 = s_cached_temp;
  telemetry.val2 = s_cached_hum;
  telemetry.battery_volt = 3.30f;
  telemetry.presence = s_cached_presence ? 1 : 0;
  strncpy(telemetry.fw_version, FIRMWARE_VERSION, sizeof(telemetry.fw_version) - 1);

  esp_now_node_send_telemetry(NULL, &telemetry);
}

static void on_presence_state_changed(bool is_detected) {
  s_cached_presence = is_detected;

  if (is_detected) {
    ESP_LOGI(TAG, "🚶 [CẢM BIẾN] Phát hiện người! -> Kích hoạt truyền ESP-NOW");
  } else {
    ESP_LOGI(TAG, "🌿 [CẢM BIẾN] Phòng trống (Hết thời gian giữ) -> Cập nhật ESP-NOW");
  }

  if (s_trigger_sem) {
    xSemaphoreGive(s_trigger_sem);
  }
}

static void on_esp_now_command_received(const uint8_t *src_mac, const NodeCommand_t *cmd) {
  if (!cmd) return;

  if (cmd->node_id != NODE_ID && cmd->node_id != 0xFF) {
    return;
  }

  ESP_LOGI(TAG, "[XỬ LÝ LỆNH TỪ GATEWAY] cmd_id=%d, param=%lu",
           cmd->cmd_id, (unsigned long)cmd->param);

  switch (cmd->cmd_id) {
  case NODE_CMD_PING:
    ESP_LOGI(TAG, "-> Lệnh Ping từ Gateway -> Gửi phản hồi");
    presence_controller_trigger_telemetry();
    break;

  case NODE_CMD_RELAY_OFF:
  case NODE_CMD_RELAY_ON:
  case NODE_CMD_RELAY_TOGGLE:
  case NODE_CMD_SET_AUTO_RELAY:
    ESP_LOGW(TAG, "-> Bỏ qua lệnh Relay: Node này không trang bị Relay!");
    break;

  case NODE_CMD_REQ_TELEMETRY:
    presence_controller_trigger_telemetry();
    break;

  case NODE_CMD_REBOOT:
    ESP_LOGW(TAG, "-> Nhận lệnh khởi động lại từ Gateway!");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    break;

  default:
    ESP_LOGW(TAG, "-> Lệnh không rõ: %d", cmd->cmd_id);
    break;
  }
}

static void presence_controller_task(void *pvParam) {
  ESP_LOGI(TAG, "Task Điều phối Node Hiện diện & SHT30 đã khởi động!");

  vTaskDelay(pdMS_TO_TICKS(1000));
  send_telemetry_packet();

  while (1) {

    if (xSemaphoreTake(s_trigger_sem, pdMS_TO_TICKS(TELEMETRY_INTERVAL_MS)) == pdTRUE) {
      ESP_LOGD(TAG, "Gửi Telemetry tức thời do có sự kiện (Hiện diện/Lệnh)");
    } else {
      ESP_LOGD(TAG, "Gửi Telemetry chu kỳ định kỳ (%d ms)", TELEMETRY_INTERVAL_MS);
    }

    send_telemetry_packet();
  }
}

esp_err_t presence_controller_init(void) {
  ESP_LOGI(TAG, "==================================================");
  ESP_LOGI(TAG, "KHỞI TẠO %s (v%s)", DEVICE_TYPE, FIRMWARE_VERSION);
  ESP_LOGI(TAG, "Node ID: %d | Node Name: %s", NODE_ID, NODE_NAME);
  ESP_LOGI(TAG, "ESP-NOW Channel: %d", ESP_NOW_WIFI_CHANNEL);
  ESP_LOGI(TAG, "==================================================");

  s_trigger_sem = xSemaphoreCreateBinary();

  led_init();
  xTaskCreate(led_task, "led_task", 2048, NULL, 4, NULL);
  led_set_espnow_connected(false);

  esp_err_t bmp_err = bmp280_init();
  if (bmp_err == ESP_OK) {
    float t = 0.0f, p = 0.0f, h = 0.0f;
    if (bmp280_read_data(&t, &p, &h) == ESP_OK) {
      s_cached_temp = t;
      s_cached_press = p;
      if (bmp280_is_bme280()) {
        s_cached_hum = h;
        s_sensor_type = ACTIVE_SENSOR_BME280;
        ESP_LOGI(TAG, "[CẢM BIẾN] Đã kết nối BME280: Temp=%.2f°C, Press=%.2f hPa, Hum=%.2f%%", t, p, h);
      } else {
        s_cached_hum = p;
        s_sensor_type = ACTIVE_SENSOR_BMP280;
        ESP_LOGI(TAG, "[CẢM BIẾN] Đã kết nối BMP280: Temp=%.2f°C, Press=%.2f hPa", t, p);
      }
    }
  } else {

    ESP_LOGD(TAG, "Không tìm thấy BMP280, thử kiểm tra SHT30...");
    esp_err_t sht_err = sht30_init();
    if (sht_err == ESP_OK) {
      float t = 0.0f, h = 0.0f;
      if (sht30_read_data(&t, &h) == ESP_OK) {
        s_cached_temp = t;
        s_cached_hum = h;
        s_sensor_type = ACTIVE_SENSOR_SHT30;
        ESP_LOGI(TAG, "[CẢM BIẾN] Đã kết nối SHT30: Temp=%.2f°C, Hum=%.2f%%", t, h);
      }
    } else {
      ESP_LOGW(TAG, "Không tìm thấy BMP280 hoặc SHT30! Hoạt động với dữ liệu mặc định.");
    }
  }

  esp_err_t radar_err = presence_sensor_init();
  if (radar_err != ESP_OK) {
    ESP_LOGE(TAG, "LD2410B UART init failed: %s", esp_err_to_name(radar_err));
    return radar_err;
  }
  presence_sensor_register_callback(on_presence_state_changed);

  ESP_ERROR_CHECK(wifi_esp_now_init(ESP_NOW_WIFI_CHANNEL));

  ESP_ERROR_CHECK(esp_now_node_init());
  esp_now_node_register_cmd_cb(on_esp_now_command_received);

  return ESP_OK;
}

void presence_controller_start(void) {
  xTaskCreate(presence_controller_task, "presence_ctrl", 3584, NULL, 5, NULL);
}

void presence_controller_trigger_telemetry(void) {
  if (s_trigger_sem) {
    xSemaphoreGive(s_trigger_sem);
  }
}

void presence_controller_get_sensor_cache(float *temp, float *hum, bool *presence) {
  if (temp) *temp = s_cached_temp;
  if (hum) *hum = s_cached_hum;
  if (presence) *presence = s_cached_presence;
}

