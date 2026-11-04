#ifndef __BMP280_H__
#define __BMP280_H__

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BMP280_I2C_ADDR           0x76
#define BMP280_I2C_ADDR_ALT       0x77
#define BMP280_I2C_ADDR_PRIM      BMP280_I2C_ADDR
#define BMP280_I2C_ADDR_SEC       BMP280_I2C_ADDR_ALT

#define BMP280_CHIP_ID_BMP280     0x58
#define BMP280_CHIP_ID_BME280     0x60
#define BMP280_CHIP_ID_CLONE1     0x56
#define BMP280_CHIP_ID_CLONE2     0x57

#ifndef BMP280_I2C_SDA_PIN
#define BMP280_I2C_SDA_PIN        8
#endif

#ifndef BMP280_I2C_SCL_PIN
#define BMP280_I2C_SCL_PIN        9
#endif

#ifndef BMP280_I2C_PORT
#define BMP280_I2C_PORT           I2C_NUM_0
#endif

#ifndef BMP280_I2C_FREQ_HZ
#define BMP280_I2C_FREQ_HZ        100000
#endif

esp_err_t bmp280_init(void);

bool bmp280_is_available(void);

bool bmp280_is_bme280(void);

uint8_t bmp280_get_chip_id(void);

uint8_t bmp280_get_i2c_addr(void);

esp_err_t bmp280_read_data(float *temperature, float *pressure, float *humidity);

#ifdef __cplusplus
}
#endif

#endif

