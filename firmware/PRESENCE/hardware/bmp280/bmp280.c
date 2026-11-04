#include "bmp280.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "BMP280";

#define BMP280_REG_CALIB_00     0x88
#define BMP280_REG_ID           0xD0
#define BMP280_REG_RESET        0xE0
#define BMP280_REG_STATUS       0xF3
#define BMP280_REG_CTRL_MEAS    0xF4
#define BMP280_REG_CONFIG       0xF5
#define BMP280_REG_PRESS_MSB    0xF7

#define BME280_REG_CTRL_HUM     0xF2
#define BME280_REG_CALIB_H1     0xA1
#define BME280_REG_CALIB_H2_H6  0xE1

typedef struct {
  uint16_t dig_T1;
  int16_t  dig_T2;
  int16_t  dig_T3;
  uint16_t dig_P1;
  int16_t  dig_P2;
  int16_t  dig_P3;
  int16_t  dig_P4;
  int16_t  dig_P5;
  int16_t  dig_P6;
  int16_t  dig_P7;
  int16_t  dig_P8;
  int16_t  dig_P9;
} bmp280_calib_t;

typedef struct {
  uint8_t  dig_H1;
  int16_t  dig_H2;
  uint8_t  dig_H3;
  int16_t  dig_H4;
  int16_t  dig_H5;
  int8_t   dig_H6;
} bme280_calib_t;

static bmp280_calib_t s_calib;
static bme280_calib_t s_bme_calib;
static int32_t        s_t_fine = 0;

static uint8_t        s_active_addr = BMP280_I2C_ADDR_PRIM;
static uint8_t        s_chip_id = 0x00;
static bool           s_driver_installed = false;
static bool           s_is_available = false;
static bool           s_is_bme280 = false;
static uint8_t        s_consecutive_fail_count = 0;

static gpio_num_t     s_sda_pin = (gpio_num_t)BMP280_I2C_SDA_PIN;
static gpio_num_t     s_scl_pin = (gpio_num_t)BMP280_I2C_SCL_PIN;

typedef struct {
  gpio_num_t  sda;
  gpio_num_t  scl;
  const char *desc;
} i2c_candidate_t;

static const i2c_candidate_t CANDIDATES[] = {
  { (gpio_num_t)BMP280_I2C_SDA_PIN, (gpio_num_t)BMP280_I2C_SCL_PIN, "Chuẩn cấu hình (SDA=GPIO8, SCL=GPIO9)" },
  { (gpio_num_t)BMP280_I2C_SCL_PIN, (gpio_num_t)BMP280_I2C_SDA_PIN, "Đảo chân (SDA=GPIO9, SCL=GPIO8 - Ngược dây SDA/SCL)" },
  { GPIO_NUM_6, GPIO_NUM_7, "Cặp GPIO 6 & 7 (SDA=GPIO6, SCL=GPIO7)" },
  { GPIO_NUM_7, GPIO_NUM_6, "Đảo cặp GPIO 6 & 7 (SDA=GPIO7, SCL=GPIO6)" },
  { GPIO_NUM_4, GPIO_NUM_5, "Cặp GPIO 4 & 5 (SDA=GPIO4, SCL=GPIO5)" },
  { GPIO_NUM_5, GPIO_NUM_4, "Đảo cặp GPIO 4 & 5 (SDA=GPIO5, SCL=GPIO4)" },
  { GPIO_NUM_0, GPIO_NUM_1, "Cặp GPIO 0 & 1 (SDA=GPIO0, SCL=GPIO1)" },
  { GPIO_NUM_1, GPIO_NUM_0, "Đảo cặp GPIO 0 & 1 (SDA=GPIO1, SCL=GPIO0)" },
  { GPIO_NUM_2, GPIO_NUM_3, "Cặp GPIO 2 & 3 (SDA=GPIO2, SCL=GPIO3)" },
  { GPIO_NUM_3, GPIO_NUM_2, "Đảo cặp GPIO 2 & 3 (SDA=GPIO3, SCL=GPIO2)" },
};

static void i2c_bus_recovery(gpio_num_t sda_io, gpio_num_t scl_io) {
  if (s_driver_installed) {
    i2c_driver_delete(BMP280_I2C_PORT);
    s_driver_installed = false;
  }

  gpio_reset_pin(sda_io);
  gpio_reset_pin(scl_io);

  gpio_set_direction(sda_io, GPIO_MODE_INPUT);
  gpio_set_pull_mode(sda_io, GPIO_PULLUP_ONLY);

  gpio_set_direction(scl_io, GPIO_MODE_OUTPUT_OD);
  gpio_set_pull_mode(scl_io, GPIO_PULLUP_ONLY);
  gpio_set_level(scl_io, 1);
  esp_rom_delay_us(10);

  for (int i = 0; i < 9; i++) {
    if (gpio_get_level(sda_io) == 1) {
      break;
    }
    gpio_set_level(scl_io, 0);
    esp_rom_delay_us(10);
    gpio_set_level(scl_io, 1);
    esp_rom_delay_us(10);
  }

  gpio_set_direction(sda_io, GPIO_MODE_OUTPUT_OD);
  gpio_set_level(sda_io, 0);
  esp_rom_delay_us(10);
  gpio_set_level(scl_io, 1);
  esp_rom_delay_us(10);
  gpio_set_level(sda_io, 1);
  esp_rom_delay_us(10);

  gpio_reset_pin(sda_io);
  gpio_reset_pin(scl_io);
}

static esp_err_t i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value) {
  uint8_t buf[2] = {reg, value};
  return i2c_master_write_to_device(BMP280_I2C_PORT, addr, buf, sizeof(buf), pdMS_TO_TICKS(50));
}

static esp_err_t i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *data, size_t len) {
  return i2c_master_write_read_device(BMP280_I2C_PORT, addr, &reg, 1, data, len, pdMS_TO_TICKS(50));
}

static bool probe_address(uint8_t addr) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_stop(cmd);
  esp_err_t err = i2c_master_cmd_begin(BMP280_I2C_PORT, cmd, pdMS_TO_TICKS(25));
  i2c_cmd_link_delete(cmd);
  return (err == ESP_OK);
}

static esp_err_t init_i2c_driver(gpio_num_t sda_io, gpio_num_t scl_io) {
  if (s_driver_installed) {
    i2c_driver_delete(BMP280_I2C_PORT);
    s_driver_installed = false;
  }

  i2c_bus_recovery(sda_io, scl_io);

  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = sda_io,
      .scl_io_num = scl_io,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = BMP280_I2C_FREQ_HZ,
  };

  esp_err_t err = i2c_param_config(BMP280_I2C_PORT, &conf);
  if (err != ESP_OK) {
    return err;
  }

  err = i2c_driver_install(BMP280_I2C_PORT, conf.mode, 0, 0, 0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;
  }

  s_driver_installed = true;
  s_sda_pin = sda_io;
  s_scl_pin = scl_io;
  return ESP_OK;
}

static esp_err_t read_calibration_data(uint8_t addr) {
  uint8_t buf[24] = {0};
  esp_err_t err = i2c_read_regs(addr, BMP280_REG_CALIB_00, buf, 24);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Đọc 24 bytes calib BMP280 thất bại: %s", esp_err_to_name(err));
    return err;
  }

  s_calib.dig_T1 = (uint16_t)(buf[0] | (buf[1] << 8));
  s_calib.dig_T2 = (int16_t)(buf[2] | (buf[3] << 8));
  s_calib.dig_T3 = (int16_t)(buf[4] | (buf[5] << 8));
  s_calib.dig_P1 = (uint16_t)(buf[6] | (buf[7] << 8));
  s_calib.dig_P2 = (int16_t)(buf[8] | (buf[9] << 8));
  s_calib.dig_P3 = (int16_t)(buf[10] | (buf[11] << 8));
  s_calib.dig_P4 = (int16_t)(buf[12] | (buf[13] << 8));
  s_calib.dig_P5 = (int16_t)(buf[14] | (buf[15] << 8));
  s_calib.dig_P6 = (int16_t)(buf[16] | (buf[17] << 8));
  s_calib.dig_P7 = (int16_t)(buf[18] | (buf[19] << 8));
  s_calib.dig_P8 = (int16_t)(buf[20] | (buf[21] << 8));
  s_calib.dig_P9 = (int16_t)(buf[22] | (buf[23] << 8));

  if (s_is_bme280) {
    uint8_t h1 = 0;
    if (i2c_read_regs(addr, BME280_REG_CALIB_H1, &h1, 1) == ESP_OK) {
      s_bme_calib.dig_H1 = h1;
    }
    uint8_t h_buf[7] = {0};
    if (i2c_read_regs(addr, BME280_REG_CALIB_H2_H6, h_buf, 7) == ESP_OK) {
      s_bme_calib.dig_H2 = (int16_t)(h_buf[0] | (h_buf[1] << 8));
      s_bme_calib.dig_H3 = (uint8_t)h_buf[2];
      s_bme_calib.dig_H4 = (int16_t)((h_buf[3] << 4) | (h_buf[4] & 0x0F));
      s_bme_calib.dig_H5 = (int16_t)((h_buf[5] << 4) | (h_buf[4] >> 4));
      s_bme_calib.dig_H6 = (int8_t)h_buf[6];
    }
  }

  return ESP_OK;
}

esp_err_t bmp280_init(void) {
  s_is_available = false;

  ESP_LOGI(TAG, "=========================================================");
  ESP_LOGI(TAG, "BẮT ĐẦU QUÉT TỰ ĐỘNG BUS I2C TÌM CẢM BIẾN...");
  ESP_LOGI(TAG, "=========================================================");

  uint8_t found_addr = 0;
  gpio_num_t found_sda = (gpio_num_t)BMP280_I2C_SDA_PIN;
  gpio_num_t found_scl = (gpio_num_t)BMP280_I2C_SCL_PIN;
  size_t total_candidates = sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);

  for (size_t i = 0; i < total_candidates; i++) {
    const i2c_candidate_t *c = &CANDIDATES[i];
    /* Never reconfigure radar UART pins during initial scan or bus recovery. */
    if (c->sda == PRESENCE_UART_RX_PIN || c->scl == PRESENCE_UART_RX_PIN ||
        c->sda == PRESENCE_UART_TX_PIN || c->scl == PRESENCE_UART_TX_PIN) {
      continue;
    }
    if (init_i2c_driver(c->sda, c->scl) != ESP_OK) {
      continue;
    }

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
      if (probe_address(addr)) {
        ESP_LOGI(TAG, "[I2C DETECTED] Tìm thấy thiết bị I2C tại địa chỉ 0x%02X trên %s!",
                 addr, c->desc);

        if (addr == BMP280_I2C_ADDR_PRIM || addr == BMP280_I2C_ADDR_SEC) {
          uint8_t test_id = 0;
          if (i2c_read_regs(addr, BMP280_REG_ID, &test_id, 1) == ESP_OK) {
            ESP_LOGI(TAG, "-> Đã đọc Chip ID = 0x%02X tại địa chỉ 0x%02X!", test_id, addr);
            found_addr = addr;
            found_sda = c->sda;
            found_scl = c->scl;
            s_chip_id = test_id;
            break;
          }
        }
      }
    }

    if (found_addr) {
      break;
    }
  }

  if (!found_addr) {
    ESP_LOGW(TAG, "[I2C SCANNER] Không tìm thấy thiết bị I2C nào phản hồi trên tất cả các cặp chân!");
    ESP_LOGW(TAG, "HÃY KIỂM TRA PHẦN CỨNG MODULE BMP280:");
    ESP_LOGW(TAG, "1. Nguồn VCC đã nối 3.3V và GND đã nối Mass?");
    ESP_LOGW(TAG, "2. Chân CSB trên module: Hãy nối CSB lên 3.3V để kích hoạt chế độ I2C (tránh bị kẹt ở SPI mode)!");
    ESP_LOGW(TAG, "3. Kiểm tra xem thứ tự chân trên bo mạch và module có bị chéo chân không.");

    if (s_driver_installed) {
      i2c_driver_delete(BMP280_I2C_PORT);
      s_driver_installed = false;
    }
    return ESP_ERR_NOT_FOUND;
  }

  s_active_addr = found_addr;
  s_sda_pin = found_sda;
  s_scl_pin = found_scl;

  if (s_chip_id == BMP280_CHIP_ID_BME280) {
    s_is_bme280 = true;
    ESP_LOGI(TAG, "Phát hiện cảm biến BME280 (Hỗ trợ Nhiệt độ + Áp suất + Độ ẩm)! (Chip ID=0x%02X)", s_chip_id);
  } else if (s_chip_id == BMP280_CHIP_ID_BMP280) {
    s_is_bme280 = false;
    ESP_LOGI(TAG, "Phát hiện cảm biến BMP280 chính hãng (Nhiệt độ + Áp suất)! (Chip ID=0x%02X)", s_chip_id);
  } else if (s_chip_id == BMP280_CHIP_ID_CLONE1 || s_chip_id == BMP280_CHIP_ID_CLONE2 || s_chip_id == 0x54) {
    s_is_bme280 = false;
    ESP_LOGI(TAG, "Phát hiện cảm biến BMP280 tương thích (Chip ID=0x%02X)!", s_chip_id);
  } else {
    ESP_LOGW(TAG, "Chip ID đọc được là 0x%02X, tiếp tục hoạt động theo chuẩn BMP280...", s_chip_id);
    s_is_bme280 = false;
  }

  i2c_write_reg(s_active_addr, BMP280_REG_RESET, 0xB6);
  vTaskDelay(pdMS_TO_TICKS(20));

  esp_err_t err = read_calibration_data(s_active_addr);
  if (err != ESP_OK) {
    if (s_driver_installed) {
      i2c_driver_delete(BMP280_I2C_PORT);
      s_driver_installed = false;
    }
    return err;
  }

  if (s_is_bme280) {
    i2c_write_reg(s_active_addr, BME280_REG_CTRL_HUM, 0x01);
  }

  uint8_t ctrl_meas = (0x02 << 5) | (0x05 << 2) | 0x03;
  err = i2c_write_reg(s_active_addr, BMP280_REG_CTRL_MEAS, ctrl_meas);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Lỗi ghi ctrl_meas: %s", esp_err_to_name(err));
    return err;
  }

  uint8_t config = (0x04 << 5) | (0x04 << 2) | 0x00;
  err = i2c_write_reg(s_active_addr, BMP280_REG_CONFIG, config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Lỗi ghi config: %s", esp_err_to_name(err));
    return err;
  }

  s_is_available = true;
  ESP_LOGI(TAG, "[THÀNH CÔNG] BMP280 sẵn sàng tại địa chỉ 0x%02X (SDA=GPIO%d, SCL=GPIO%d)!",
           s_active_addr, s_sda_pin, s_scl_pin);

  return ESP_OK;
}

bool bmp280_is_available(void) {
  return s_is_available;
}

bool bmp280_is_bme280(void) {
  return s_is_bme280;
}

uint8_t bmp280_get_chip_id(void) {
  return s_chip_id;
}

uint8_t bmp280_get_i2c_addr(void) {
  return s_active_addr;
}

esp_err_t bmp280_read_data(float *temperature, float *pressure, float *humidity) {
  if (!s_is_available) {
    return ESP_FAIL;
  }

  uint8_t data[8] = {0};
  size_t read_len = s_is_bme280 ? 8 : 6;

  esp_err_t err = i2c_read_regs(s_active_addr, BMP280_REG_PRESS_MSB, data, read_len);
  if (err != ESP_OK) {
    s_consecutive_fail_count++;
    ESP_LOGW(TAG, "Đọc dữ liệu BMP280 thất bại: %s (Lần %d)",
             esp_err_to_name(err), s_consecutive_fail_count);

    if (s_consecutive_fail_count >= 5) {
      ESP_LOGW(TAG, "Tự động khôi phục bus I2C do lỗi liên tiếp...");
      bmp280_init();
      s_consecutive_fail_count = 0;
    }
    return err;
  }

  s_consecutive_fail_count = 0;

  int32_t adc_P = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) | ((int32_t)data[2] >> 4);
  int32_t adc_T = ((int32_t)data[3] << 12) | ((int32_t)data[4] << 4) | ((int32_t)data[5] >> 4);

  int32_t var1 = ((((adc_T >> 3) - ((int32_t)s_calib.dig_T1 << 1))) * ((int32_t)s_calib.dig_T2)) >> 11;
  int32_t var2 = (((((adc_T >> 4) - ((int32_t)s_calib.dig_T1)) *
                    ((adc_T >> 4) - ((int32_t)s_calib.dig_T1))) >> 12) *
                  ((int32_t)s_calib.dig_T3)) >> 14;
  s_t_fine = var1 + var2;
  int32_t T = (s_t_fine * 5 + 128) >> 8;
  if (temperature) {
    *temperature = (float)T / 100.0f;
  }

  if (pressure) {
    int64_t p_var1 = ((int64_t)s_t_fine) - 128000;
    int64_t p_var2 = p_var1 * p_var1 * (int64_t)s_calib.dig_P6;
    p_var2 = p_var2 + ((p_var1 * (int64_t)s_calib.dig_P5) << 17);
    p_var2 = p_var2 + (((int64_t)s_calib.dig_P4) << 35);
    p_var1 = ((p_var1 * p_var1 * (int64_t)s_calib.dig_P3) >> 8) + ((p_var1 * (int64_t)s_calib.dig_P2) << 12);
    p_var1 = (((((int64_t)1) << 47) + p_var1)) * ((int64_t)s_calib.dig_P1) >> 33;

    if (p_var1 != 0) {
      int64_t p = 1048576 - adc_P;
      p = (((p << 31) - p_var2) * 3125) / p_var1;
      p_var1 = (((int64_t)s_calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
      p_var2 = (((int64_t)s_calib.dig_P8) * p) >> 19;
      p = ((p + p_var1 + p_var2) >> 8) + (((int64_t)s_calib.dig_P7) << 4);
      *pressure = (float)p / 256.0f / 100.0f;
    } else {
      *pressure = 0.0f;
    }
  }

  if (humidity) {
    if (s_is_bme280) {
      int32_t adc_H = ((int32_t)data[6] << 8) | ((int32_t)data[7]);
      int32_t v_x1_u32r = (s_t_fine - ((int32_t)76800));
      v_x1_u32r = (((((adc_H << 14) - (((int32_t)s_bme_calib.dig_H4) << 20) -
                      (((int32_t)s_bme_calib.dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) *
                   (((((((v_x1_u32r * ((int32_t)s_bme_calib.dig_H6)) >> 10) *
                        (((v_x1_u32r * ((int32_t)s_bme_calib.dig_H3)) >> 11) + ((int32_t)32768))) >> 10) +
                      ((int32_t)2097152)) * ((int32_t)s_bme_calib.dig_H2) + 8192) >> 14));
      v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
                                 ((int32_t)s_bme_calib.dig_H1)) >> 4));
      if (v_x1_u32r < 0) v_x1_u32r = 0;
      if (v_x1_u32r > 419430400) v_x1_u32r = 419430400;
      *humidity = (float)(v_x1_u32r >> 12) / 1024.0f;
      if (*humidity > 100.0f) *humidity = 100.0f;
    } else {

      *humidity = 0.0f;
    }
  }

  return ESP_OK;
}

