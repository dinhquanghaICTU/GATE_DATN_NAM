#ifndef __SHT30_H__
#define __SHT30_H__

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHT30_I2C_ADDR 0x44
#define SHT30_I2C_SDA_IO GPIO_NUM_8
#define SHT30_I2C_SCL_IO GPIO_NUM_9
#define SHT30_I2C_PORT I2C_NUM_0
#define SHT30_I2C_FREQ_HZ 100000

esp_err_t sht30_init(void);

esp_err_t sht30_read_data(float *temperature, float *humidity);

#ifdef __cplusplus
}
#endif

#endif // __SHT30_H__