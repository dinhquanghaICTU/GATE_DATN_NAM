#include "relay.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "HARDWARE_RELAY";

#ifndef RELAY_1_PIN
#define RELAY_1_PIN GPIO_NUM_38
#endif

#ifndef RELAY_2_PIN
#define RELAY_2_PIN GPIO_NUM_39
#endif

static bool s_relay_1_state = false;
static bool s_relay_2_state = false;

void relay_init(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << RELAY_1_PIN) | (1ULL << RELAY_2_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);
  relay_turn_off();
  relay_set_channel(2, false);
  ESP_LOGI(TAG, "Khoi tao Relay tren GPIO %d va %d thanh cong!", RELAY_1_PIN, RELAY_2_PIN);
}

void relay_turn_on(void) {
  ESP_LOGI(TAG, "⚡ Bat Relay 1");
  gpio_set_level((gpio_num_t)RELAY_1_PIN, 1);
  s_relay_1_state = true;
}

void relay_turn_off(void) {
  ESP_LOGI(TAG, "⚡ Tat Relay 1");
  gpio_set_level((gpio_num_t)RELAY_1_PIN, 0);
  s_relay_1_state = false;
}

void relay_toggle(void) {
  if (s_relay_1_state) {
    relay_turn_off();
  } else {
    relay_turn_on();
  }
}

bool relay_is_on(void) {
  return s_relay_1_state;
}

void relay_set_channel(int ch, bool state) {
  if (ch == 1) {
    gpio_set_level((gpio_num_t)RELAY_1_PIN, state ? 1 : 0);
    s_relay_1_state = state;
  } else if (ch == 2) {
    gpio_set_level((gpio_num_t)RELAY_2_PIN, state ? 1 : 0);
    s_relay_2_state = state;
  }
}

bool relay_get_channel(int ch) {
  if (ch == 1) return s_relay_1_state;
  if (ch == 2) return s_relay_2_state;
  return false;
}
