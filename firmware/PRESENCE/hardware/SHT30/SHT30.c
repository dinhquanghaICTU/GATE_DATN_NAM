#include "SHT30.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SHT30";

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

esp_err_t sht30_init(void) {
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
    ESP_LOGE(TAG, "Cấu hình I2C thất bại: %s", esp_err_to_name(err));
    return err;
  }

  err = i2c_driver_install(SHT30_I2C_PORT, conf.mode, 0, 0, 0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Cài đặt I2C driver thất bại: %s", esp_err_to_name(err));
    return err;
  }

  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (SHT30_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_stop(cmd);
  err = i2c_master_cmd_begin(SHT30_I2C_PORT, cmd, pdMS_TO_TICKS(50));
  i2c_cmd_link_delete(cmd);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Không tìm thấy cảm biến SHT30 tại địa chỉ 0x%02X (lỗi: %s)",
             SHT30_I2C_ADDR, esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "Khởi tạo thành công (Addr: 0x%02X, SDA: GPIO%d, SCL: GPIO%d)",
           SHT30_I2C_ADDR, SHT30_I2C_SDA_IO, SHT30_I2C_SCL_IO);
  return ESP_OK;
}

esp_err_t sht30_read_data(float *temperature, float *humidity) {
  if (!temperature || !humidity) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t cmd[2] = {0x24, 0x00};
  esp_err_t err = i2c_master_write_to_device(
      SHT30_I2C_PORT, SHT30_I2C_ADDR, cmd, sizeof(cmd), pdMS_TO_TICKS(50));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Gửi lệnh đo thất bại: %s", esp_err_to_name(err));
    return err;
  }

  vTaskDelay(pdMS_TO_TICKS(20));

  uint8_t data[6] = {0};
  err = i2c_master_read_from_device(SHT30_I2C_PORT, SHT30_I2C_ADDR, data,
                                    sizeof(data), pdMS_TO_TICKS(50));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Đọc dữ liệu thất bại: %s", esp_err_to_name(err));
    return err;
  }

  if (sht30_crc8(&data[0], 2) != data[2]) {
    ESP_LOGE(TAG, "Lỗi CRC nhiệt độ!");
    return ESP_ERR_INVALID_CRC;
  }
  if (sht30_crc8(&data[3], 2) != data[5]) {
    ESP_LOGE(TAG, "Lỗi CRC độ ẩm!");
    return ESP_ERR_INVALID_CRC;
  }

  uint16_t raw_temp = ((uint16_t)data[0] << 8) | data[1];
  uint16_t raw_hum = ((uint16_t)data[3] << 8) | data[4];

  *temperature = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
  *humidity = 100.0f * ((float)raw_hum / 65535.0f);

  if (*humidity > 100.0f)
    *humidity = 100.0f;
  if (*humidity < 0.0f)
    *humidity = 0.0f;

  return ESP_OK;
}
