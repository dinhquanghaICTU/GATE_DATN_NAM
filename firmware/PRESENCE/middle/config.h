#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <stdint.h>

/* ==================== SYSTEM & FIRMWARE INFO ==================== */
#define FIRMWARE_VERSION          "1.0.0"
#define DEVICE_TYPE               "ESP32-C3 Presence Node"
#define NODE_ID                   1
#define NODE_NAME                 "PRESENCE_01"

/* ==================== ESP-NOW WIRELESS CONFIG ==================== */
#define ESP_NOW_WIFI_CHANNEL      1      // Channel phai trung voi trạm Gateway S3
#define TELEMETRY_INTERVAL_MS     2000   // Chu kỳ gửi gói tin định kỳ (ms)

/* ==================== HARDWARE PIN CONFIGURATION (ESP32-C3 SUPER MINI) ==================== */
// Onboard Blue LED trên board ESP32-C3 Super Mini (Active LOW: 0 là BẬT, 1 là TẮT)
#define STATUS_LED_PIN            8

// Onboard BOOT Button trên board ESP32-C3 Super Mini (Active LOW)
#define BUTTON_PIN                9

// Chân tín hiệu Cảm biến Hiện diện (Digital OUT từ mmWave Radar LD2410 / RCWL / PIR)
#define PRESENCE_SENSOR_PIN       4

// Chân điều khiển Relay / Tải bật đèn trực tiếp (Tùy chọn)
#define RELAY_PIN                 3

#endif /* __CONFIG_H__ */
