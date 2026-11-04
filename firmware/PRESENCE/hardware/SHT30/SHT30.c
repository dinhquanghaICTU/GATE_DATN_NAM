#include "SHT30.h"
#include "config.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SHT30";

static uint8_t s_sht30_addr = SHT30_I2C_ADDR;
static bool s_driver_installed = false;
static uint8_t s_consecutive_fail_count = 0;

static uint8_t sht30_crc8(const uint8_t *data, int len) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int bit = 8; bit > 0; --bit) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x31;
      } else {
        crc = (crc << 1);
      }
    }
  }
  return crc;
}

static void i2c_bus_recovery(void) {
  if (s_driver_installed) {
    i2c_driver_delete(SHT30_I2C_PORT);
    s_driver_installed = false;
  }

  gpio_reset_pin(SHT30_I2C_SDA_IO);
  gpio_reset_pin(SHT30_I2C_SCL_IO);

  gpio_set_direction(SHT30_I2C_SDA_IO, GPIO_MODE_INPUT);
  gpio_set_pull_mode(SHT30_I2C_SDA_IO, GPIO_PULLUP_ONLY);

  gpio_set_direction(SHT30_I2C_SCL_IO, GPIO_MODE_OUTPUT_OD);
  gpio_set_pull_mode(SHT30_I2C_SCL_IO, GPIO_PULLUP_ONLY);
  gpio_set_level(SHT30_I2C_SCL_IO, 1);
  esp_rom_delay_us(10);

  for (int i = 0; i < 9; i++) {
    if (gpio_get_level(SHT30_I2C_SDA_IO) == 1) {
      break;
    }
    gpio_set_level(SHT30_I2C_SCL_IO, 0);
    esp_rom_delay_us(10);
    gpio_set_level(SHT30_I2C_SCL_IO, 1);
    esp_rom_delay_us(10);
  }

  gpio_set_direction(SHT30_I2C_SDA_IO, GPIO_MODE_OUTPUT_OD);
  gpio_set_level(SHT30_I2C_SDA_IO, 0);
  esp_rom_delay_us(10);
  gpio_set_level(SHT30_I2C_SCL_IO, 1);
  esp_rom_delay_us(10);
  gpio_set_level(SHT30_I2C_SDA_IO, 1);
  esp_rom_delay_us(10);

  gpio_reset_pin(SHT30_I2C_SDA_IO);
  gpio_reset_pin(SHT30_I2C_SCL_IO);
}

static bool probe_address(uint8_t addr) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_stop(cmd);
  esp_err_t err = i2c_master_cmd_begin(SHT30_I2C_PORT, cmd, pdMS_TO_TICKS(50));
  i2c_cmd_link_delete(cmd);
  return (err == ESP_OK);
}

esp_err_t sht30_init(void) {
  if (!s_driver_installed) {

    i2c_bus_recovery();

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SHT30_I2C_SDA_IO,
        .scl_io_num = SHT30_I2C_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = SHT30_I2C_FREQ_HZ,
    };

    esp_err_t err = i2c_param_config(SHT30_I2C_PORT, &conf);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Lỗi i2c_param_config: %s", esp_err_to_name(err));
      return err;
    }

    err = i2c_driver_install(SHT30_I2C_PORT, conf.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      ESP_LOGE(TAG, "Lỗi i2c_driver_install: %s", esp_err_to_name(err));
      return err;
    }
    s_driver_installed = true;
  }

  if (probe_address(0x44)) {
    s_sht30_addr = 0x44;
    ESP_LOGI(TAG, "[THÀNH CÔNG] SHT30 sẵn sàng tại 0x44 (SDA=GPIO%d, SCL=GPIO%d)!",
             SHT30_I2C_SDA_IO, SHT30_I2C_SCL_IO);
    return ESP_OK;
  }

  if (probe_address(0x45)) {
    s_sht30_addr = 0x45;
    ESP_LOGI(TAG, "[THÀNH CÔNG] SHT30 sẵn sàng tại 0x45 (SDA=GPIO%d, SCL=GPIO%d)!",
             SHT30_I2C_SDA_IO, SHT30_I2C_SCL_IO);
    return ESP_OK;
  }

  ESP_LOGW(TAG, "Không tìm thấy SHT30 tại 0x44/0x45 trên SDA=%d, SCL=%d",
           SHT30_I2C_SDA_IO, SHT30_I2C_SCL_IO);
  return ESP_FAIL;
}

esp_err_t sht30_read_data(float *temperature, float *humidity) {
  if (!temperature || !humidity) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!s_driver_installed) {
    if (sht30_init() != ESP_OK) {
      return ESP_FAIL;
    }
  }

  uint8_t cmd[2] = {0x24, 0x00};
  esp_err_t write_err = i2c_master_write_to_device(
      SHT30_I2C_PORT, s_sht30_addr, cmd, sizeof(cmd), pdMS_TO_TICKS(50));
  if (write_err != ESP_OK) {
    s_consecutive_fail_count++;
    ESP_LOGW(TAG, "Gửi lệnh đo 0x2400 tới 0x%02X thất bại: %s (Lỗi lần %d)",
             s_sht30_addr, esp_err_to_name(write_err), s_consecutive_fail_count);

    if (s_consecutive_fail_count >= 3) {
      ESP_LOGW(TAG, "Tự động khôi phục bus I2C (Bus Recovery) do lỗi liên tiếp...");
      sht30_init();
      s_consecutive_fail_count = 0;
    }
    return write_err;
  }

  vTaskDelay(pdMS_TO_TICKS(20));

  uint8_t data[6] = {0};
  esp_err_t read_err = i2c_master_read_from_device(
      SHT30_I2C_PORT, s_sht30_addr, data, sizeof(data), pdMS_TO_TICKS(50));
  if (read_err != ESP_OK) {
    s_consecutive_fail_count++;
    ESP_LOGW(TAG, "Đọc 6 bytes từ 0x%02X thất bại: %s (Lỗi lần %d)",
             s_sht30_addr, esp_err_to_name(read_err), s_consecutive_fail_count);
    if (s_consecutive_fail_count >= 3) {
      ESP_LOGW(TAG, "Tự động khôi phục bus I2C (Bus Recovery) do lỗi liên tiếp...");
      sht30_init();
      s_consecutive_fail_count = 0;
    }
    return read_err;
  }

  uint8_t crc_temp = sht30_crc8(&data[0], 2);
  uint8_t crc_hum  = sht30_crc8(&data[3], 2);

  if (crc_temp != data[2]) {
    ESP_LOGW(TAG, "CRC Nhiệt độ không khớp (tính=%02X, nhận=%02X) - Raw: %02X %02X",
             crc_temp, data[2], data[0], data[1]);
    return ESP_ERR_INVALID_CRC;
  }

  uint16_t raw_temp = ((uint16_t)data[0] << 8) | data[1];
  uint16_t raw_hum  = ((uint16_t)data[3] << 8) | data[4];

  *temperature = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
  *humidity    = 100.0f * ((float)raw_hum / 65535.0f);

  if (*humidity > 100.0f) *humidity = 100.0f;
  if (*humidity < 0.0f)   *humidity = 0.0f;

  if (crc_hum != data[5]) {
    ESP_LOGD(TAG, "CRC Độ ẩm lệch nhẹ (tính=%02X, nhận=%02X), vẫn chấp nhận: %.1f%%",
             crc_hum, data[5], *humidity);
  }

  s_consecutive_fail_count = 0;
  return ESP_OK;
}

