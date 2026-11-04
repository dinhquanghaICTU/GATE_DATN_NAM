#include "led.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HARDWARE_LED";

#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 10
#endif

#ifndef LED_ACTIVE_LEVEL
#define LED_ACTIVE_LEVEL 0
#endif

#if LED_ACTIVE_LEVEL == 1
#define LED_ON_LEVEL  1
#define LED_OFF_LEVEL 0
#else
#define LED_ON_LEVEL  0
#define LED_OFF_LEVEL 1
#endif

static led_state_t s_current_led_state = LED_STATE_DISCONNECTED;
static volatile bool s_activity_pulse = false;

void led_init(void) {
#if STATUS_LED_PIN >= 0
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << STATUS_LED_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);
  gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_OFF_LEVEL);
  s_current_led_state = LED_STATE_DISCONNECTED;
  ESP_LOGI(TAG, "Khoi tao Status LED tren GPIO %d thanh cong (Active %s)!",
           STATUS_LED_PIN, (LED_ACTIVE_LEVEL == 1) ? "HIGH" : "LOW");
#else
  ESP_LOGI(TAG, "LED bị vô hiệu hóa.");
#endif
}

void led_set_state(led_state_t state) { s_current_led_state = state; }

void led_set_espnow_connected(bool connected) {
  s_current_led_state = connected ? LED_STATE_CONNECTED : LED_STATE_DISCONNECTED;
}

led_state_t led_get_state(void) { return s_current_led_state; }

void led_notify_activity(void) { s_activity_pulse = true; }

void led_task(void *pvParam) {
#if STATUS_LED_PIN >= 0
  ESP_LOGI(TAG, "LED Task da khoi dong tren GPIO %d (Chế độ giám sát kết nối ESP-NOW)!", STATUS_LED_PIN);

  while (1) {
    switch (s_current_led_state) {
    case LED_STATE_CONNECTED:
      /* Kết nối ESP-NOW thành công -> GIỮ SÁNG LIÊN TỤC */
      gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_ON_LEVEL);
      vTaskDelay(pdMS_TO_TICKS(100));
      break;

    case LED_STATE_DISCONNECTED:
    case LED_STATE_ERROR:
    default:
      /* Chưa kết nối / mất kết nối ESP-NOW: Giữ sáng 1.5s rồi chớp tắt 0.5s */
      gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_ON_LEVEL);
      for (int i = 0; i < 15 && s_current_led_state != LED_STATE_CONNECTED; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      if (s_current_led_state != LED_STATE_CONNECTED) {
        gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_OFF_LEVEL);
        for (int i = 0; i < 5 && s_current_led_state != LED_STATE_CONNECTED; i++) {
          vTaskDelay(pdMS_TO_TICKS(100));
        }
      }
      break;

    case LED_STATE_OFF:
      gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_OFF_LEVEL);
      vTaskDelay(pdMS_TO_TICKS(200));
      break;
    }
  }
#else
  vTaskDelete(NULL);
#endif
}

