#include "button.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "m_state_machine.h"

static const char *TAG = "HARDWARE_BUTTON";
static TaskHandle_t s_btn_task_handle = NULL;

#ifndef BUTTON_PIN
#define BUTTON_PIN 21
#endif

#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN 0
#endif

static void btn_gpio_init(uint32_t pin) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << pin),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);
}

void button_task(void *pvParam) {
  s_btn_task_handle = xTaskGetCurrentTaskHandle();
  ESP_LOGI(TAG, "Button Task dang chay quet tren GPIO %d va GPIO %d (BOOT)...", BUTTON_PIN, BOOT_BUTTON_PIN);

  btn_gpio_init(BUTTON_PIN);
  btn_gpio_init(BOOT_BUTTON_PIN);

  const uint32_t pins[2] = {BUTTON_PIN, BOOT_BUTTON_PIN};
  uint32_t press_ms[2] = {0, 0};
  bool hold_3s_fired[2] = {false, false};
  bool hold_10s_fired[2] = {false, false};

  while (1) {
    for (int i = 0; i < 2; i++) {
      int level = gpio_get_level((gpio_num_t)pins[i]);
      if (level == 0) {
        press_ms[i] += 20;

        if (press_ms[i] == 60) {
          ESP_LOGI(TAG, "[NUT BAM] Phat hien nhan nut tren GPIO %lu!", (unsigned long)pins[i]);
        }

        if (press_ms[i] >= 3000 && !hold_3s_fired[i]) {
          hold_3s_fired[i] = true;
          ESP_LOGW(TAG, "[NUT BAM] Da giu 3s tren GPIO %lu -> XOA WI-FI VA CHUYEN BLE CONFIG NGAY!", (unsigned long)pins[i]);
          m_state_machine_reset_wifi();
        }

        if (press_ms[i] >= 10000 && !hold_10s_fired[i]) {
          hold_10s_fired[i] = true;
          ESP_LOGW(TAG, "[NUT BAM] Da giu 10s tren GPIO %lu -> KHOI PHUC CAI DAT GOC VA REBOOT!", (unsigned long)pins[i]);
          nvs_erase_wifi_credentials();
          vTaskDelay(pdMS_TO_TICKS(500));
          esp_restart();
        }
      } else {
        if (press_ms[i] >= 60 && !hold_3s_fired[i]) {
          ESP_LOGI(TAG, "[NUT BAM] Nhan nha (%lu ms tren GPIO %lu)",
                   (unsigned long)press_ms[i], (unsigned long)pins[i]);
        }
        press_ms[i] = 0;
        hold_3s_fired[i] = false;
        hold_10s_fired[i] = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

esp_err_t button_init(void) {
  btn_gpio_init(BUTTON_PIN);
  btn_gpio_init(BOOT_BUTTON_PIN);
  ESP_LOGI(TAG, "Khoi tao Nut bam tren GPIO %d (Ngoai) va GPIO %d (BOOT Onboard) thanh cong!", BUTTON_PIN, BOOT_BUTTON_PIN);
  return ESP_OK;
}

void button_task_stop(void) {
  if (s_btn_task_handle != NULL) {
    vTaskDelete(s_btn_task_handle);
    s_btn_task_handle = NULL;
    ESP_LOGW(TAG, "Da dung Button Task de uu tien nap OTA!");
  }
}

void button_task_start(void) {
  if (s_btn_task_handle == NULL) {
    xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Da bat lai Button Task!");
  }
}

