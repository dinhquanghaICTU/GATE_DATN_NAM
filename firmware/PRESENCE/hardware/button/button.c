#include "button.h"
#include "app_btn.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"

static const char *TAG = "HARDWARE_BUTTON";
static TaskHandle_t s_btn_task_handle = NULL;

#ifndef BUTTON_PIN
#define BUTTON_PIN 9
#endif

static uint32_t btn_get_tick_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

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

static uint32_t btn_gpio_read(uint32_t pin) {
  return gpio_get_level((gpio_num_t)pin);
}

static void on_btn_event(int pin, int event, void *data) {

  if (event == APP_BTN_EVT_HOLD) {
    uint32_t *hold_time = (uint32_t *)data;
    uint32_t ms = hold_time ? *hold_time : 3000;
    ESP_LOGW(TAG, "[BOOT BUTTON] Nhấn giữ %lu ms!", (unsigned long)ms);
  } else if (event == APP_BTN_EVT_HOLD_LONG) {
    ESP_LOGW(TAG, "[BOOT BUTTON] Nhấn giữ 10s -> Khởi động lại thiết bị!");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
  }
}

void button_task(void *pvParam) {
  s_btn_task_handle = xTaskGetCurrentTaskHandle();
  ESP_LOGI(TAG, "Button Task đang chạy quét trên GPIO %d...", BUTTON_PIN);

  while (1) {
    app_btn_scan(NULL);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

esp_err_t button_init(void) {
  static app_btn_hw_config_t hw_btn = {
      .pin = BUTTON_PIN,
      .idle_level = 1,
  };

  app_btn_config_t cfg = {
      .config = &hw_btn,
      .btn_count = 1,
      .get_tick_cb = btn_get_tick_ms,
      .btn_initialize = btn_gpio_init,
      .btn_read = btn_gpio_read,
  };

  app_btn_initialize(&cfg);
  app_btn_register_callback(APP_BTN_EVT_PRESSED, on_btn_event, NULL);
  app_btn_register_callback(APP_BTN_EVT_HOLD, on_btn_event, NULL);
  app_btn_register_callback(APP_BTN_EVT_HOLD_LONG, on_btn_event, NULL);

  ESP_LOGI(TAG, "Khởi tạo Nút bấm trên GPIO %d thành công!", BUTTON_PIN);
  return ESP_OK;
}

void button_task_stop(void) {
  if (s_btn_task_handle != NULL) {
    vTaskDelete(s_btn_task_handle);
    s_btn_task_handle = NULL;
  }
}

void button_task_start(void) {
  if (s_btn_task_handle == NULL) {
    xTaskCreate(button_task, "button_task", 3072, NULL, 5, NULL);
  }
}
