#include "relay.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "HARDWARE_RELAY";

static bool s_relay_state = false;
static bool s_auto_mode = AUTO_RELAY_DEFAULT_ENABLE;

#ifndef RELAY_PIN
#define RELAY_PIN 3
#endif

#ifndef RELAY_ACTIVE_LEVEL
#define RELAY_ACTIVE_LEVEL 1
#endif

static inline int get_gpio_level(bool on) {
  if (RELAY_ACTIVE_LEVEL == 1) {
    return on ? 1 : 0;
  } else {
    return on ? 0 : 1;
  }
}

esp_err_t relay_init(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << RELAY_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };

  esp_err_t err = gpio_config(&io_conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Lỗi cấu hình GPIO %d cho Relay: %s", RELAY_PIN, esp_err_to_name(err));
    return err;
  }

  s_relay_state = false;
  gpio_set_level((gpio_num_t)RELAY_PIN, get_gpio_level(false));
  ESP_LOGI(TAG, "Khởi tạo Relay trên GPIO %d thành công (Auto Mode: %s)!",
           RELAY_PIN, s_auto_mode ? "BẬT" : "TẮT");
  return ESP_OK;
}

void relay_turn_on(void) {
  s_relay_state = true;
  gpio_set_level((gpio_num_t)RELAY_PIN, get_gpio_level(true));
  ESP_LOGI(TAG, "[RELAY] BẬT tải đèn trên GPIO %d", RELAY_PIN);
}

void relay_turn_off(void) {
  s_relay_state = false;
  gpio_set_level((gpio_num_t)RELAY_PIN, get_gpio_level(false));
  ESP_LOGI(TAG, "[RELAY] TẮT tải đèn trên GPIO %d", RELAY_PIN);
}

void relay_toggle(void) {
  relay_set_state(!s_relay_state);
}

void relay_set_state(bool on) {
  if (on) {
    relay_turn_on();
  } else {
    relay_turn_off();
  }
}

bool relay_is_on(void) {
  return s_relay_state;
}

void relay_set_auto_mode(bool enable) {
  s_auto_mode = enable;
  ESP_LOGI(TAG, "Cập nhật Chế độ Auto-Relay: %s", enable ? "BẬT" : "TẮT");
}

bool relay_get_auto_mode(void) {
  return s_auto_mode;
}

