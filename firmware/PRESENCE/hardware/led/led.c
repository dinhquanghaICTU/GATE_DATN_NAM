#include "led.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HARDWARE_LED";

#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 8
#endif

#define LED_ON_LEVEL 0
#define LED_OFF_LEVEL 1

static led_state_t s_current_led_state = LED_STATE_IDLE;
static volatile bool s_activity_pulse = false;

void led_init(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << STATUS_LED_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);
  gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_OFF_LEVEL);
  ESP_LOGI(TAG, "Khoi tao Onboard LED tren GPIO %d thanh cong!",
           STATUS_LED_PIN);
}

void led_set_state(led_state_t state) { s_current_led_state = state; }

led_state_t led_get_state(void) { return s_current_led_state; }

void led_notify_activity(void) { s_activity_pulse = true; }

void led_task(void *pvParam) {
  ESP_LOGI(TAG, "LED Task da khoi dong tren GPIO %d!", STATUS_LED_PIN);

  while (1) {
    if (s_activity_pulse) {
      s_activity_pulse = false;
      gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_ON_LEVEL);
      vTaskDelay(pdMS_TO_TICKS(50));
      gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_OFF_LEVEL);
      vTaskDelay(pdMS_TO_TICKS(50));
    }

    switch (s_current_led_state) {
    case LED_STATE_OFF:
      gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_OFF_LEVEL);
      vTaskDelay(pdMS_TO_TICKS(200));
      break;

    case LED_STATE_PRESENCE_ACTIVE:
      // Co nguoi: Sang lien tuc
      gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_ON_LEVEL);
      vTaskDelay(pdMS_TO_TICKS(100));
      break;

    case LED_STATE_IDLE:
      // Cho / khong co nguoi: Chop nhe 1 lan moi 2 giay de bao thiet bi song
      gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_ON_LEVEL);
      vTaskDelay(pdMS_TO_TICKS(30));
      gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_OFF_LEVEL);
      vTaskDelay(pdMS_TO_TICKS(1970));
      break;

    case LED_STATE_TRANSMITTING:
      gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_ON_LEVEL);
      vTaskDelay(pdMS_TO_TICKS(80));
      gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_OFF_LEVEL);
      vTaskDelay(pdMS_TO_TICKS(80));
      break;

    case LED_STATE_ERROR:
      // Bao loi: Chop nhanh
      gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_ON_LEVEL);
      vTaskDelay(pdMS_TO_TICKS(100));
      gpio_set_level((gpio_num_t)STATUS_LED_PIN, LED_OFF_LEVEL);
      vTaskDelay(pdMS_TO_TICKS(100));
      break;

    default:
      vTaskDelay(pdMS_TO_TICKS(200));
      break;
    }
  }
}
