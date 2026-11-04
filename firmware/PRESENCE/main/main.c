#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>

#include "config.h"
#include "presence_controller.h"
#include "presence_sensor.h"

static const char *TAG = "PRESENCE_MAIN";

/* Giám sát dữ liệu Radar: chỉ in khi có thay đổi trạng thái hoặc định kỳ mỗi 8s khi có người */
static void presence_uart_test_task(void *arg) {
  static const char *const states[] = {"TRỐNG", "CHUYỂN ĐỘNG", "ĐỨNG YÊN", "CẢ HAI"};
  uint8_t last_state = 0xFF;
  uint32_t counter = 0;

  while (1) {
    presence_sensor_data_t data;
    if (presence_sensor_get_data(&data) && data.online) {
      bool state_changed = (data.radar.target_state != last_state);
      last_state = data.radar.target_state;

      if (state_changed || (data.radar.target_state != 0 && (++counter % 4 == 0))) {
        if (data.radar.target_state == 0) {
          ESP_LOGI(TAG, "📡 [RADAR] 🌿 Khu vực TRỐNG (Không có mục tiêu)");
        } else {
          ESP_LOGI(TAG, "📡 [RADAR] %-12s | Cự ly: %3u cm | Động: %2u%% | Tĩnh: %2u%%",
                   states[data.radar.target_state],
                   data.radar.detection_distance_cm,
                   data.radar.moving_energy,
                   data.radar.stationary_energy);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void app_main(void) {

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      ESP_LOGI(
          TAG,
          "Xác nhận phân vùng OTA '%s' khởi động thành công -> Hủy rollback!",
          running->label);
      esp_ota_mark_app_valid_cancel_rollback();
    }
  }

  ESP_LOGI(TAG, "=========================================================");
  ESP_LOGI(TAG, "🚀 KHỞI ĐỘNG %s (v%s)", DEVICE_TYPE, FIRMWARE_VERSION);
  ESP_LOGI(TAG, "   Node ID: %d | ESP-NOW Channel: %d", NODE_ID, ESP_NOW_WIFI_CHANNEL);
  ESP_LOGI(TAG, "   BMP280 / SHT30 : SDA=GPIO%d, SCL=GPIO%d", BMP280_I2C_SDA_PIN, BMP280_I2C_SCL_PIN);
  ESP_LOGI(TAG, "   LD2410B Radar  : RX=GPIO%d, TX=GPIO%d", PRESENCE_UART_RX_PIN, PRESENCE_UART_TX_PIN);
  ESP_LOGI(TAG, "   Relay Tải Đèn  : GPIO%d | Status LED: GPIO%d", RELAY_PIN, STATUS_LED_PIN);
  ESP_LOGI(TAG, "=========================================================");

  ESP_ERROR_CHECK(presence_controller_init());

  presence_controller_start();
  if (xTaskCreate(presence_uart_test_task, "radar_test", 3072, NULL, 2,
                  NULL) != pdPASS) {
    ESP_LOGE(TAG, "Cannot create LD2410B test task");
  }

  ESP_LOGI(TAG, "✨ Node Cảm biến Hiện diện & Môi trường đã sẵn sàng hoạt động!");
}

