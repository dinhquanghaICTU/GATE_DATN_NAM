#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

#include "ble.h"
#include "button.h"
#include "config.h"
#include "esp_now_node.h"
#include "flash.h"
#include "gate_controller.h"
#include "led.h"
#include "m_state_machine.h"
#include "mqtt.h"
#include "ota.h"
#include "relay.h"
#include "wifi.h"

static const char *TAG = "GATE_MAIN";

static void print_system_info(void) {
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  uint32_t flash_size = 0;
  esp_flash_get_size(NULL, &flash_size);

  ESP_LOGI(TAG, "==========================================================");
  ESP_LOGI(TAG, "      ESP32-S3 GATEWAY FIRMWARE (DATN_NAM)             ");
  ESP_LOGI(TAG, "      Phiên bản: %s | IDF: %s                             ",
           FIRMWARE_VERSION, esp_get_idf_version());
  ESP_LOGI(TAG, "      Chip: %s (%d Cores, Rev %d)                        ",
           CONFIG_IDF_TARGET, chip_info.cores, chip_info.revision);
  ESP_LOGI(TAG, "      Flash: %lu MB | Free SRAM: %lu bytes               ",
           (unsigned long)(flash_size / (1024 * 1024)),
           (unsigned long)esp_get_free_heap_size());
  ESP_LOGI(TAG, "==========================================================");
}

static void verify_ota_rollback_status(void) {
  const esp_partition_t *running_part = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
  bool is_new_ota_verified = false;

  if (running_part &&
      esp_ota_get_state_partition(running_part, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      ESP_LOGI(
          TAG,
          "[OTA FIRST BOOT] Ứng dụng mới boot lần đầu, xác nhận hợp lệ...");
      esp_ota_mark_app_valid_cancel_rollback();
      is_new_ota_verified = true;
    }
  }

  nvs_handle_t nvs;
  if (nvs_open("gate_cfg", NVS_READWRITE, &nvs) == ESP_OK) {
    char pending[32] = {0};
    size_t len = sizeof(pending);
    if (nvs_get_str(nvs, "fw_pending", pending, &len) == ESP_OK && len > 1) {
      if (is_new_ota_verified) {
        nvs_set_str(nvs, "fw_ver", pending);
        ESP_LOGI(TAG, "Đã xác nhận phiên bản firmware mới hợp lệ: [%s]",
                 pending);
      } else {
        ESP_LOGE(TAG,
                 "[ROLLBACK PHÁT HIỆN] Bản firmware [%s] khởi động lỗi, đã "
                 "rollback về an toàn!",
                 pending);
        nvs_set_str(nvs, "fw_rollback", pending);
      }
      nvs_erase_key(nvs, "fw_pending");
      nvs_commit(nvs);
    }
    nvs_close(nvs);
  }
}

void app_main(void) {
  print_system_info();

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS partition bị phân mảnh/phiên bản mới, đang xóa và khởi "
                  "tạo lại...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  verify_ota_rollback_status();

  led_init();
  button_init();
  relay_init();

  wifi_init();
  app_mqtt_init(NULL);
  esp_now_node_init();

  gate_controller_init();
  m_state_machine_init();

  xTaskCreate(m_state_machine_task, "m_state_machine", 8192, NULL, 5, NULL);
  xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);
  xTaskCreate(led_task, "led_task", 2048, NULL, 2, NULL);
  xTaskCreate(gate_controller_task, "gate_ctrl_task", 8192, NULL, 3, NULL);

  ESP_LOGI(TAG, "Hệ thống Gateway ESP32-S3 đã sẵn sàng!");
}
