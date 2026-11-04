#ifndef __SHT30_H__
#define __SHT30_H__

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHT30_I2C_ADDR 0x44

#ifndef SHT30_I2C_SDA_IO
#define SHT30_I2C_SDA_IO ((gpio_num_t)SHT30_I2C_SDA_PIN)
#endif

#ifndef SHT30_I2C_SCL_IO
#define SHT30_I2C_SCL_IO ((gpio_num_t)SHT30_I2C_SCL_PIN)
#endif

#ifndef SHT30_I2C_PORT
#define SHT30_I2C_PORT I2C_NUM_0
#endif

#ifndef SHT30_I2C_FREQ_HZ
#define SHT30_I2C_FREQ_HZ 100000
#endif

esp_err_t sht30_init(void);

esp_err_t sht30_read_data(float *temperature, float *humidity);

#ifdef __cplusplus
}
#endif

#endif
