#include "SHT30.h"
#include "ble.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>

static const char *TAG = "APP_MAIN";

static void sht30_task(void *pvParameters) {
  float temp = 0.0f;
  float hum = 0.0f;

  while (1) {
    if (sht30_read_data(&temp, &hum) == ESP_OK) {
      ESP_LOGI(TAG, "SHT30 -> Nhiệt độ: %.2f °C | Độ ẩm: %.2f %%", temp, hum);
      ble_node_update_sensor_data(temp, hum, false);
    } else {
      ESP_LOGE(TAG, "Lỗi đọc dữ liệu cảm biến SHT30!");
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

  ESP_LOGI(TAG, "Khoi dong ESP32-C3 Presence & Sensor Node...");

  ESP_ERROR_CHECK(sht30_init());

  ESP_ERROR_CHECK(ble_node_init("PRESENCE_C3"));

  xTaskCreate(sht30_task, "sht30_task", 3072, NULL, 5, NULL);
}
