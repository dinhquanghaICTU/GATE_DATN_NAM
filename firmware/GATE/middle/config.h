#ifndef __CONFIG_H__
#define __CONFIG_H__

#define FIRMWARE_VERSION "1.0.0"
#define DEVICE_TYPE "ESP32-S3 Gateway"
#define BLE_DEVICE_NAME "GATE_S3_CONFIG"

// Nạp thông tin bảo mật từ secrets.h (nếu có)
#if __has_include("secrets.h")
#include "secrets.h"
#endif

// Giá trị mặc định nếu secrets.h không có hoặc bị Git bỏ qua
#ifndef HUNONIC_USER_PHONE
#define HUNONIC_USER_PHONE "0358868830"
#endif

#ifndef HUNONIC_USER_PASS
#define HUNONIC_USER_PASS ""
#endif

#define DEFAULT_WIFI_SSID "QuangHa"
#define DEFAULT_WIFI_PASS "66668888"

#define MQTT_BROKER_URI                                                        \
  "mqtts://20476a36ce36478d90de6d5676587638.s1.eu.hivemq.cloud:8883"
#define MQTT_USERNAME "quanghaictu"
#define MQTT_PASSWORD "Zdinhquangha1234"

#define TOPIC_GATE_COMMAND "gate/command"
#define TOPIC_GATE_STATUS "gate/status"
#define TOPIC_GATE_TELEMETRY "gate/telemetry"
#define TOPIC_GATE_OTA "gate/ota"

#define BUTTON_PIN 21
#define LED_WS2812_PIN 48
#define RELAY_1_PIN 38
#define RELAY_2_PIN 39

#endif /* __CONFIG_H__ */
