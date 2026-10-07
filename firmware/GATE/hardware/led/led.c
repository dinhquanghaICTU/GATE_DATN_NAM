#include "led.h"
#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

static const char *TAG = "HARDWARE_LED";

#ifndef LED_WS2812_PIN
#define LED_WS2812_PIN 48
#endif

static led_strip_handle_t s_led_strip = NULL;
static led_state_t s_current_led_state = LED_STATE_OFF;
static volatile bool s_activity_pulse = false;

void led_init(void) {
  led_strip_config_t strip_config = {
      .strip_gpio_num = LED_WS2812_PIN,
      .max_leds = 1,
      .led_model = LED_MODEL_WS2812,
      .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
      .flags = {
          .invert_out = false,
      },
  };

  led_strip_rmt_config_t rmt_config = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 10 * 1000 * 1000,
      .flags = {
          .with_dma = false,
      },
  };

  ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip));
  led_strip_clear(s_led_strip);
  ESP_LOGI(TAG, "Khoi tao LED WS2812 tren GPIO %d thanh cong!", LED_WS2812_PIN);
}

static void led_set_rgb(uint32_t red, uint32_t green, uint32_t blue) {
  if (s_led_strip) {
    led_strip_set_pixel(s_led_strip, 0, red, green, blue);
    led_strip_refresh(s_led_strip);
  }
}

void led_set_state(led_state_t state) {
  s_current_led_state = state;
}

led_state_t led_get_state(void) {
  return s_current_led_state;
}

void led_notify_activity(void) {
  s_activity_pulse = true;
}

void led_task(void *pvParam) {
  ESP_LOGI(TAG, "LED Task da khoi dong!");

  while (1) {
    if (s_activity_pulse) {
      s_activity_pulse = false;
      led_set_rgb(0, 80, 80); // Cyan blink on packet activity
      vTaskDelay(pdMS_TO_TICKS(80));
      led_set_rgb(0, 0, 0);
      vTaskDelay(pdMS_TO_TICKS(40));
    }

    switch (s_current_led_state) {
    case LED_STATE_OFF:
      led_set_rgb(0, 0, 0);
      vTaskDelay(pdMS_TO_TICKS(200));
      break;

    case LED_STATE_BLE_CONFIG:
      led_set_rgb(0, 0, 60); // Blue blink
      vTaskDelay(pdMS_TO_TICKS(500));
      led_set_rgb(0, 0, 0);
      vTaskDelay(pdMS_TO_TICKS(500));
      break;

    case LED_STATE_WIFI_CONNECTING:
      led_set_rgb(50, 40, 0); // Yellow fast blink
      vTaskDelay(pdMS_TO_TICKS(200));
      led_set_rgb(0, 0, 0);
      vTaskDelay(pdMS_TO_TICKS(200));
      break;

    case LED_STATE_WIFI_DISCONNECTED:
      led_set_rgb(60, 0, 0); // Red slow blink
      vTaskDelay(pdMS_TO_TICKS(500));
      led_set_rgb(0, 0, 0);
      vTaskDelay(pdMS_TO_TICKS(500));
      break;

    case LED_STATE_ONLINE_OK:
      led_set_rgb(0, 30, 0); // Steady subtle green
      vTaskDelay(pdMS_TO_TICKS(200));
      break;

    case LED_STATE_GATE_ACTIVE:
      led_set_rgb(0, 40, 40); // Cyan
      vTaskDelay(pdMS_TO_TICKS(200));
      led_set_rgb(0, 0, 0);
      vTaskDelay(pdMS_TO_TICKS(200));
      break;

    case LED_STATE_OTA_UPDATING:
      led_set_rgb(60, 0, 60); // Fast Magenta blink
      vTaskDelay(pdMS_TO_TICKS(100));
      led_set_rgb(0, 0, 0);
      vTaskDelay(pdMS_TO_TICKS(100));
      break;

    case LED_STATE_ERROR:
      led_set_rgb(80, 0, 0); // Solid Red
      vTaskDelay(pdMS_TO_TICKS(200));
      break;

    default:
      vTaskDelay(pdMS_TO_TICKS(100));
      break;
    }
  }
}
