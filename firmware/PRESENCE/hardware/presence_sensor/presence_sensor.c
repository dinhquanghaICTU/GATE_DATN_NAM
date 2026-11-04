#include "presence_sensor.h"
#include "config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* UART0 console would drive GPIO21 while the radar uses these pins. */
#if CONFIG_ESP_CONSOLE_UART && (CONFIG_ESP_CONSOLE_UART_NUM == 0)
#error "PRESENCE radar uses GPIO20/21: select USB Serial/JTAG console in menuconfig"
#endif

static const char *TAG = "PRESENCE_SENSOR";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static presence_sensor_data_t s_data;
static uint32_t s_hold_off_ms = PRESENCE_HOLD_OFF_MS;
static int64_t s_last_report_us;
static int64_t s_empty_since_us;
static int64_t s_active_start_us;
static presence_state_cb_t s_state_cb;
static TaskHandle_t s_task;

static void accept_report(const ld2410_report_t *report) {
  int64_t now = esp_timer_get_time();
  presence_state_cb_t cb;
  bool changed;
  bool detected;
  portENTER_CRITICAL(&s_lock);
  /* A UART outage is unknown occupancy, never evidence of an empty room. */
  if (!s_data.frame_count || now - s_last_report_us > PRESENCE_UART_TIMEOUT_MS * 1000LL)
    s_empty_since_us = 0;
  bool previous = s_data.detected;
  s_data.radar = *report;
  s_data.frame_count++;
  s_last_report_us = now;
  if (report->target_state != 0) {
    s_empty_since_us = 0;
    if (!previous) s_active_start_us = now;
    s_data.detected = true;
  } else if (previous) {
    if (!s_empty_since_us) s_empty_since_us = now;
    if (now - s_empty_since_us >= s_hold_off_ms * 1000LL)
      s_data.detected = false;
  }
  detected = s_data.detected;
  changed = previous != detected;
  cb = s_state_cb;
  portEXIT_CRITICAL(&s_lock);
  if (changed) {
    ESP_LOGI(TAG, "LD2410B: %s", detected ? "CO NGUOI" : "PHONG TRONG");
    if (cb) cb(detected);
  }
}

static void presence_uart_task(void *arg) {
  ld2410_parser_t parser = {0};
  uint8_t bytes[128];
  int64_t last_byte_us = 0;
  while (1) {
    int count = uart_read_bytes(PRESENCE_UART_PORT_NUM, bytes, sizeof(bytes),
                                pdMS_TO_TICKS(100));
    int64_t now = esp_timer_get_time();
    /* Discard a truncated frame after an inter-byte gap. */
    if (now - last_byte_us > 250000) parser.used = 0;
    if (count > 0) {
      last_byte_us = now;
      for (int i = 0; i < count; i++) {
        ld2410_report_t report;
        if (ld2410_parser_feed(&parser, bytes[i], &report)) accept_report(&report);
      }
    } else if (count < 0) {
      ESP_LOGW(TAG, "UART read failed");
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

esp_err_t presence_sensor_init(void) {
  if (s_task) return ESP_OK;
  const uart_config_t cfg = {
      .baud_rate = PRESENCE_UART_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  esp_err_t err = uart_driver_install(PRESENCE_UART_PORT_NUM, 2048, 0, 0, NULL, 0);
  if (err != ESP_OK) return err;
  err = uart_param_config(PRESENCE_UART_PORT_NUM, &cfg);
  if (err == ESP_OK)
    err = uart_set_pin(PRESENCE_UART_PORT_NUM, PRESENCE_UART_TX_PIN,
                       PRESENCE_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    uart_driver_delete(PRESENCE_UART_PORT_NUM);
    return err;
  }
  if (xTaskCreate(presence_uart_task, "presence_uart", 3072, NULL, 6, &s_task) != pdPASS) {
    uart_driver_delete(PRESENCE_UART_PORT_NUM);
    s_task = NULL;
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "LD2410B UART%d: RX=GPIO%d TX=GPIO%d, %d baud 8N1",
           PRESENCE_UART_PORT_NUM, PRESENCE_UART_RX_PIN, PRESENCE_UART_TX_PIN,
           PRESENCE_UART_BAUD_RATE);
  return ESP_OK;
}

bool presence_sensor_get_data(presence_sensor_data_t *out) {
  if (!out) return false;
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  *out = s_data;
  int64_t age = (now - s_last_report_us) / 1000;
  out->age_ms = age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
  out->online = s_data.frame_count > 0 && age <= PRESENCE_UART_TIMEOUT_MS;
  bool valid = s_data.frame_count > 0;
  portEXIT_CRITICAL(&s_lock);
  return valid;
}

void presence_sensor_register_callback(presence_state_cb_t cb) {
  portENTER_CRITICAL(&s_lock);
  s_state_cb = cb;
  portEXIT_CRITICAL(&s_lock);
}

bool presence_sensor_is_detected(void) {
  portENTER_CRITICAL(&s_lock);
  bool detected = s_data.detected;
  portEXIT_CRITICAL(&s_lock);
  return detected;
}

uint32_t presence_sensor_get_active_duration_sec(void) {
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  uint32_t seconds = s_data.detected ? (uint32_t)((now - s_active_start_us) / 1000000) : 0;
  portEXIT_CRITICAL(&s_lock);
  return seconds;
}

void presence_sensor_set_hold_off_ms(uint32_t ms) {
  portENTER_CRITICAL(&s_lock);
  s_hold_off_ms = ms;
  portEXIT_CRITICAL(&s_lock);
}

uint32_t presence_sensor_get_hold_off_ms(void) {
  portENTER_CRITICAL(&s_lock);
  uint32_t ms = s_hold_off_ms;
  portEXIT_CRITICAL(&s_lock);
  return ms;
}
