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

/* Reads the driver's snapshot only; it never consumes bytes from the radar UART. */
static void presence_uart_test_task(void *arg) {
  static const char *const states[] = {"EMPTY", "MOVING", "STATIONARY", "BOTH"};
  while (1) {
    presence_sensor_data_t data;
    if (!presence_sensor_get_data(&data) || !data.online) {
      ESP_LOGW(TAG, "[LD2410B TEST] No fresh UART report. Check TX->GPIO%d, "
                    "RX->GPIO%d, common GND and %d baud", PRESENCE_UART_RX_PIN,
               PRESENCE_UART_TX_PIN, PRESENCE_UART_BAUD_RATE);
    } else {
      ESP_LOGI(TAG, "[LD2410B TEST] %s presence=%d moving=%u cm/%u "
                    "still=%u cm/%u distance=%u cm frames=%lu age=%lu ms",
               states[data.radar.target_state], data.detected,
               data.radar.moving_distance_cm, data.radar.moving_energy,
               data.radar.stationary_distance_cm, data.radar.stationary_energy,
               data.radar.detection_distance_cm, (unsigned long)data.frame_count,
               (unsigned long)data.age_ms);
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
  ESP_LOGI(TAG, "KHỞI ĐỘNG %s", DEVICE_TYPE);
  ESP_LOGI(TAG, "Firmware: v%s | Target: ESP32-C3 Super Mini",
           FIRMWARE_VERSION);
  ESP_LOGI(TAG, "Node ID: %d | ESP-NOW Channel: %d", NODE_ID,
           ESP_NOW_WIFI_CHANNEL);
  ESP_LOGI(TAG, "BMP280 / SHT30: SDA=GPIO%d, SCL=GPIO%d", BMP280_I2C_SDA_PIN,
           BMP280_I2C_SCL_PIN);
  ESP_LOGI(TAG, "LD2410B UART: RX=GPIO%d, TX=GPIO%d | Relay: GPIO%d",
           PRESENCE_UART_RX_PIN, PRESENCE_UART_TX_PIN, RELAY_PIN);
  ESP_LOGI(TAG, "Status LED: GPIO%d (Đã loại bỏ Button)", STATUS_LED_PIN);


  ESP_ERROR_CHECK(presence_controller_init());

  presence_controller_start();
  if (xTaskCreate(presence_uart_test_task, "radar_test", 3072, NULL, 2,
                  NULL) != pdPASS) {
    ESP_LOGE(TAG, "Cannot create LD2410B test task");
  }

  ESP_LOGI(TAG,
           "Node Cảm biến Hiện diện & Môi trường (BMP280) đã sẵn sàng giao tiếp ESP-NOW!");
}

