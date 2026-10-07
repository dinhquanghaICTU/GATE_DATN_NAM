#include "gate_controller.h"
#include "config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "esp_now_node.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hunonic_bridge.h"
#include "jsmn.h"
#include "led.h"
#include "mqtt.h"
#include "ota.h"
#include "relay.h"
#include "wifi.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "APP_GATE_CTRL";

static void on_hunonic_ac_state(const hunonic_ac_device_t *device, void *arg) {
  if (!device)
    return;
  char resp[256];
  snprintf(resp, sizeof(resp),
           "{\"event\":\"ac_state\",\"id\":\"%s\",\"name\":\"%s\",\"power\":%d,"
           "\"temp\":%d,\"mode\":%d,\"fan\":%d}",
           device->id, device->name, device->power, device->temp, device->mode,
           device->fan);
#ifdef TOPIC_GATE_STATUS
  app_mqtt_publish(TOPIC_GATE_STATUS, resp, 0, 0);
#endif
}

static void on_esp_now_telemetry(const uint8_t *mac,
                                 const NodeTelemetry_t *data, int rssi) {
  if (!data)
    return;

  char json_buf[384];
  snprintf(
      json_buf, sizeof(json_buf),
      "{\"node_id\":%u,\"packet_id\":%lu,\"mac\":\"%02X:%02X:%02X:%02X:%02X:%"
      "02X\","
      "\"val1\":%.2f,\"val2\":%.2f,\"battery\":%.2f,\"fw\":\"%s\",\"rssi\":%d}",
      data->node_id, (unsigned long)data->packet_id, mac[0], mac[1], mac[2],
      mac[3], mac[4], mac[5], data->val1, data->val2, data->battery_volt,
      data->fw_version, rssi);

  ESP_LOGI(TAG, "Forwarding telemetry -> MQTT: %s", json_buf);

#ifdef TOPIC_GATE_TELEMETRY
  app_mqtt_publish(TOPIC_GATE_TELEMETRY, json_buf, 0, 0);
#endif

  led_notify_activity();
}

static void on_mqtt_command_received(const char *topic, int topic_len,
                                     const char *data, int data_len,
                                     void *arg) {
  if (!data || data_len <= 0)
    return;

  char payload[512] = {0};
  if (data_len >= sizeof(payload))
    return;
  memcpy(payload, data, data_len);
  payload[data_len] = '\0';

  ESP_LOGI(TAG, "Xu ly MQTT Command: %s", payload);

  jsmn_parser parser;
  jsmntok_t tokens[32];
  int r = json_parser(payload, &parser, tokens, 32);
  if (r < 0) {
    ESP_LOGE(TAG, "Loi cu phap JSON tu MQTT!");
    return;
  }

  char cmd[32] = {0};
  json_get_str(payload, tokens, r, "cmd", cmd, sizeof(cmd));

  if (strcmp(cmd, "relay") == 0) {
    int ch = json_get_int(payload, tokens, r, "ch");
    int state = json_get_int(payload, tokens, r, "state");
    ESP_LOGI(TAG, "Dieu khien Relay kenh %d -> %d", ch, state);
    relay_set_channel(ch > 0 ? ch : 1, state > 0);

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"event\":\"relay_state\",\"ch\":%d,\"state\":%d}",
             ch > 0 ? ch : 1, relay_get_channel(ch > 0 ? ch : 1));
#ifdef TOPIC_GATE_STATUS
    app_mqtt_publish(TOPIC_GATE_STATUS, resp, 1, 0);
#endif
  } else if (strcmp(cmd, "node_cmd") == 0) {
    int node_id = json_get_int(payload, tokens, r, "node_id");
    int cmd_id = json_get_int(payload, tokens, r, "cmd_id");
    int param = json_get_int(payload, tokens, r, "param");

    NodeCommand_t node_cmd = {
        .cmd_id = (uint8_t)cmd_id,
        .node_id = (uint8_t)node_id,
        .param = (uint32_t)param,
    };
    ESP_LOGI(TAG, "Gui lenh xuong Node #%d qua ESP-NOW: cmd=%d, param=%d",
             node_id, cmd_id, param);
    esp_now_node_send_command(NULL, &node_cmd);
  } else if (strcmp(cmd, "ac") == 0 || strcmp(cmd, "hunonic_ac") == 0) {
    char dev_id[16] = {0};
    json_get_str(payload, tokens, r, "id", dev_id, sizeof(dev_id));
    int power = json_get_int(payload, tokens, r, "power");
    int temp = json_get_int(payload, tokens, r, "temp");
    int mode = json_get_int(payload, tokens, r, "mode");
    int fan = json_get_int(payload, tokens, r, "fan");

    const hunonic_ac_device_t *ac_devs = hunonic_bridge_get_devices(NULL);
    const char *target_id =
        (strlen(dev_id) > 0)
            ? dev_id
            : (ac_devs && ac_devs[0].is_valid ? ac_devs[0].id : "3992007");

    ESP_LOGI(TAG,
             "Dieu khien Hunonic AC [%s]: Power=%d, Temp=%d, Mode=%d, Fan=%d",
             target_id, power, temp, mode, fan);

    esp_err_t ret = hunonic_bridge_control(target_id, power,
                                           temp > 0 ? temp : 25, mode, fan);

    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"event\":\"ac_state\",\"id\":\"%s\",\"power\":%d,\"temp\":%d,"
             "\"mode\":%d,\"fan\":%d,\"result\":%s}",
             target_id, power, temp > 0 ? temp : 25, mode, fan,
             ret == ESP_OK ? "\"ok\"" : "\"fail\"");
#ifdef TOPIC_GATE_STATUS
    app_mqtt_publish(TOPIC_GATE_STATUS, resp, 1, 0);
#endif
  } else if (strcmp(cmd, "get_devices") == 0 || strcmp(cmd, "sync") == 0) {
    int ac_count = 0;
    const hunonic_ac_device_t *ac_list = hunonic_bridge_get_devices(&ac_count);
    char resp[512];
    int len = snprintf(resp, sizeof(resp),
                       "{\"event\":\"device_list\",\"relay1\":%d,\"relay2\":%d,"
                       "\"ac_count\":%d",
                       relay_get_channel(1), relay_get_channel(2), ac_count);
    if (ac_count > 0 && ac_list && ac_list[0].is_valid) {
      len += snprintf(resp + len, sizeof(resp) - len,
                      ",\"ac\":{\"id\":\"%s\",\"name\":\"%s\",\"power\":%d,"
                      "\"temp\":%d,\"mode\":%d,\"fan\":%d,\"brand\":%d}",
                      ac_list[0].id, ac_list[0].name, ac_list[0].power,
                      ac_list[0].temp, ac_list[0].mode, ac_list[0].fan,
                      ac_list[0].brand);
    }
    snprintf(resp + len, sizeof(resp) - len, "}");
#ifdef TOPIC_GATE_STATUS
    app_mqtt_publish(TOPIC_GATE_STATUS, resp, 1, 0);
#endif
  } else if (strcmp(cmd, "reboot") == 0) {
    ESP_LOGW(TAG, "Nhan lenh Reboot tu MQTT! Khoi dong lai sau 1 giay...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
  } else if (strcmp(cmd, "ota") == 0) {
    ota_config_t ota_cfg;
    if (ota_parse_json(payload, &ota_cfg) == ESP_OK) {
      ESP_LOGI(TAG, "Bat dau OTA tu MQTT Command...");
      ota_start(&ota_cfg);
    }
  }
}

esp_err_t gate_controller_init(void) {
  esp_now_node_register_telemetry_cb(on_esp_now_telemetry);
  app_mqtt_register_data_cb(on_mqtt_command_received, NULL);
  hunonic_bridge_register_state_cb(on_hunonic_ac_state, NULL);
  ESP_LOGI(TAG, "Gate Controller da khoi tao thanh cong!");
  return ESP_OK;
}
/*
    task này để bắn lên mqtt giữ kết nối, và luôn check
    xem task gọi tới endpoint của hunonic kiểm tra xem có online hay không
    nếu bị văng ra gọi func kết nối lại,
*/
void gate_controller_task(void *pvParam) {
  ESP_LOGI(TAG, "Gate Controller Heartbeat Task bat dau!");
  static uint32_t s_periodic_sync_counter = 0;

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    s_periodic_sync_counter += 5;

    if (s_periodic_sync_counter >= 30) {
      s_periodic_sync_counter = 0;
      if (wifi_is_connected()) {
        hunonic_bridge_sync_devices();
      }
    }

    if (app_mqtt_is_connected()) {
      wifi_ap_record_t ap_info;
      int8_t rssi = -100;
      if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
      }

      uint32_t free_heap = esp_get_free_heap_size();
      uint32_t uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000);
      uint32_t total_packets = esp_now_node_get_total_packets();

      int ac_count = 0;
      const hunonic_ac_device_t *ac_list =
          hunonic_bridge_get_devices(&ac_count);
      int ac_p = (ac_count > 0 && ac_list && ac_list[0].is_valid)
                     ? ac_list[0].power
                     : 0;
      int ac_t = (ac_count > 0 && ac_list && ac_list[0].is_valid)
                     ? ac_list[0].temp
                     : 25;
      const char *ac_id = (ac_count > 0 && ac_list && ac_list[0].is_valid)
                              ? ac_list[0].id
                              : "3992007";
      const char *ac_name = (ac_count > 0 && ac_list && ac_list[0].is_valid)
                                ? ac_list[0].name
                                : "Dieu hoa";

      char status_buf[384];
      snprintf(
          status_buf, sizeof(status_buf),
          "{\"event\":\"heartbeat\",\"device\":\"esp32s3_gate\","
          "\"uptime_sec\":%lu,\"free_heap\":%lu,\"rssi\":%d,"
          "\"relay1\":%d,\"relay2\":%d,\"ac_id\":\"%s\",\"ac_name\":\"%s\","
          "\"ac_power\":%d,\"ac_temp\":%d,\"ws_connected\":%d,"
          "\"total_node_pkts\":%lu,\"fw_ver\":\"%s\"}",
          (unsigned long)uptime_sec, (unsigned long)free_heap, (int)rssi,
          relay_get_channel(1), relay_get_channel(2), ac_id, ac_name, ac_p,
          ac_t, hunonic_bridge_is_connected() ? 1 : 0,
          (unsigned long)total_packets, ota_get_current_version());

#ifdef TOPIC_GATE_STATUS
      app_mqtt_publish(TOPIC_GATE_STATUS, status_buf, 0, 0);
#endif
    }
  }
}
