#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <stdint.h>
#include <stdbool.h>

#define FIRMWARE_VERSION          "1.0.0"
#define DEVICE_TYPE               "ESP32-C3 Presence & BMP280 Node"
#define NODE_ID                   1
#define NODE_NAME                 "PRESENCE_01"

#define ESP_NOW_WIFI_CHANNEL      1
#define TELEMETRY_INTERVAL_MS     2500
#define GATEWAY_MAC_ADDR          {0x28, 0x84, 0x85, 0x97, 0xE2, 0x28}

#ifndef BMP280_I2C_SDA_PIN
#define BMP280_I2C_SDA_PIN        8
#endif

#ifndef BMP280_I2C_SCL_PIN
#define BMP280_I2C_SCL_PIN        9
#endif

#ifndef SHT30_I2C_SDA_PIN
#define SHT30_I2C_SDA_PIN         BMP280_I2C_SDA_PIN
#endif

#ifndef SHT30_I2C_SCL_PIN
#define SHT30_I2C_SCL_PIN         BMP280_I2C_SCL_PIN
#endif

#define BMP280_I2C_PORT_NUM       0
#define BMP280_I2C_FREQ_HZ        100000
#define SHT30_I2C_PORT_NUM        0
#define SHT30_I2C_FREQ_HZ         100000


#ifndef PRESENCE_UART_RX_PIN
#define PRESENCE_UART_RX_PIN      20
#endif
#ifndef PRESENCE_UART_TX_PIN
#define PRESENCE_UART_TX_PIN      21
#endif
#define PRESENCE_UART_PORT_NUM    1
#define PRESENCE_UART_BAUD_RATE   256000
#define PRESENCE_UART_TIMEOUT_MS  2000
#define PRESENCE_HOLD_OFF_MS      4000

#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN            10
#endif

#ifndef LED_ACTIVE_LEVEL
#define LED_ACTIVE_LEVEL          1
#endif

/* Bỏ nút bấm Button */
#ifndef BUTTON_PIN
#define BUTTON_PIN                -1
#endif

#endif

