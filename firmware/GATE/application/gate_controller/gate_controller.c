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
#include "wifi.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "APP_GATE_CTRL";

static void on_hunonic_ac_state(const hunonic_ac_device_t *device, void *arg) {
  if (!device)
    return;
  char resp[256];
  snprintf(resp, sizeof(resp),
           "{\"event\":\"ac_state\",\"id\":\"%s\",\"name\":\"%s\",\"brand\":%d,\"power\":%d,"
           "\"temp\":%d,\"mode\":%d,\"fan\":%d}",
           device->id, device->name, device->brand, device->power, device->temp, device->mode,
           device->fan);
#ifdef TOPIC_GATE_STATUS
  app_mqtt_publish(TOPIC_GATE_STATUS, resp, 0, 0);
#endif
}

static void on_esp_now_telemetry(const uint8_t *mac,
                                 const NodeTelemetry_t *data, int rssi) {
  if (!data)
    return;

  static uint32_t s_last_packet_id = 0;
  if (data->packet_id != 0 && data->packet_id == s_last_packet_id) {
    return;
  }
  s_last_packet_id = data->packet_id;

  char json_buf[384];

  snprintf(
      json_buf, sizeof(json_buf),
      "{\"node_id\":%u,\"packet_id\":%lu,\"mac\":\"%02X:%02X:%02X:%02X:%02X:%"
      "02X\","
      "\"val1\":%.2f,\"val2\":%.2f,\"battery\":%.2f,\"presence\":%u,\"fw\":\"%s\",\"rssi\":%d}",
      data->node_id, (unsigned long)data->packet_id, mac[0], mac[1], mac[2],
      mac[3], mac[4], mac[5], data->val1, data->val2, data->battery_volt,
      data->presence, data->fw_version, rssi);

  ESP_LOGI(TAG, "Forwarding telemetry -> MQTT: %s", json_buf);

#ifdef TOPIC_GATE_TELEMETRY
  app_mqtt_publish(TOPIC_GATE_TELEMETRY, json_buf, 0, 0);
#endif

  led_notify_activity();
}

typedef struct {
  char payload[512];
} gate_mqtt_cmd_t;

static QueueHandle_t s_mqtt_cmd_queue = NULL;

static void execute_gate_command(const char *payload) {
  if (!payload || strlen(payload) == 0)
    return;

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
  if (cmd[0] == '\0') {
    json_get_str(payload, tokens, r, "action", cmd, sizeof(cmd));
  }

  if (strcmp(cmd, "node_cmd") == 0) {
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

    int ac_count_avail = 0;
    const hunonic_ac_device_t *ac_devs = hunonic_bridge_get_devices(&ac_count_avail);
    const char *target_id = NULL;

    if (strlen(dev_id) > 0) {
      target_id = dev_id;
    } else if (ac_count_avail > 0 && ac_devs && ac_devs[0].is_valid) {
      target_id = ac_devs[0].id;
    }

    if (!target_id || strlen(target_id) == 0) {
      ESP_LOGW(TAG, "Khong tim thay thiet bi dieu hoa nao de dieu khien!");
      char resp[128];
      snprintf(resp, sizeof(resp),
               "{\"event\":\"ac_state\",\"error\":\"no_device\",\"result\":\"fail\"}");
#ifdef TOPIC_GATE_STATUS
      app_mqtt_publish(TOPIC_GATE_STATUS, resp, 0, 0);
#endif
      return;
    }

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
    app_mqtt_publish(TOPIC_GATE_STATUS, resp, 0, 0);
#endif
  } else if (strcmp(cmd, "get_devices") == 0 || strcmp(cmd, "sync") == 0) {
    if (strcmp(cmd, "sync") == 0) {
      ESP_LOGI(TAG, "Nhan lenh SYNC tu MQTT -> Goi Hunonic Cloud lay danh sach thiet bi moi nhat...");
      esp_err_t sync_err = hunonic_bridge_sync_devices();
      if (sync_err != ESP_OK) {
        int count = 0;
        hunonic_bridge_get_devices(&count);
        char diagnostic[192];
        snprintf(diagnostic, sizeof(diagnostic),
                 "{\"event\":\"hunonic_sync_result\",\"result\":\"fail\","
                 "\"err\":%d,\"http_status\":%d,\"ac_count\":%d,"
                 "\"ws_connected\":%d,\"ir_online\":%d}",
                 (int)hunonic_bridge_get_last_sync_error(),
                 hunonic_bridge_get_last_sync_http_status(), count,
                 hunonic_bridge_is_connected() ? 1 : 0,
                 hunonic_bridge_is_ir_hub_online() ? 1 : 0);
#ifdef TOPIC_GATE_STATUS
        app_mqtt_publish(TOPIC_GATE_STATUS, diagnostic, 0, 0);
#endif
        ESP_LOGW(TAG, "Hunonic sync failed; diagnostic MQTT response published");
        return;
      }
    }
    int ac_count = 0;
    const hunonic_ac_device_t *ac_list = hunonic_bridge_get_devices(&ac_count);
    char *resp = (char *)malloc(1536);
    if (resp) {
      int len = snprintf(resp, 1536,
                         "{\"event\":\"device_list\","
                         "\"ac_count\":%d,\"devices\":[",
                         ac_count);
      bool first = true;
      for (int i = 0; i < ac_count; i++) {
        if (ac_list[i].is_valid) {
          len += snprintf(resp + len, 1536 - len,
                          "%s{\"id\":\"%s\",\"name\":\"%s\",\"power\":%d,"
                          "\"temp\":%d,\"mode\":%d,\"fan\":%d,\"brand\":%d}",
                          (!first ? "," : ""),
                          ac_list[i].id, ac_list[i].name, ac_list[i].power,
                          ac_list[i].temp, ac_list[i].mode, ac_list[i].fan,
                          ac_list[i].brand);
          first = false;
        }
      }
      len += snprintf(resp + len, 1536 - len, "]");
      if (ac_count > 0 && ac_list && ac_list[0].is_valid) {
        len += snprintf(resp + len, 1536 - len,
                        ",\"ac\":{\"id\":\"%s\",\"name\":\"%s\",\"power\":%d,"
                        "\"temp\":%d,\"mode\":%d,\"fan\":%d,\"brand\":%d}",
                        ac_list[0].id, ac_list[0].name, ac_list[0].power,
                        ac_list[0].temp, ac_list[0].mode, ac_list[0].fan,
                        ac_list[0].brand);
      }
      snprintf(resp + len, 1536 - len, "}");
#ifdef TOPIC_GATE_STATUS
      app_mqtt_publish(TOPIC_GATE_STATUS, resp, 0, 0);
#endif
      free(resp);
    }
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

static void gate_cmd_worker_task(void *pvParam) {
  ESP_LOGI(TAG, "Gate Command Worker Task khoi dong thanh cong!");
  gate_mqtt_cmd_t item;
  while (1) {
    if (xQueueReceive(s_mqtt_cmd_queue, &item, portMAX_DELAY) == pdTRUE) {

      gate_mqtt_cmd_t next_item;
      while (xQueuePeek(s_mqtt_cmd_queue, &next_item, 0) == pdTRUE) {
        if ((strstr(item.payload, "\"hunonic_ac\"") || strstr(item.payload, "\"cmd\": \"ac\"") || strstr(item.payload, "\"cmd\":\"ac\"")) &&
            (strstr(next_item.payload, "\"hunonic_ac\"") || strstr(next_item.payload, "\"cmd\": \"ac\"") || strstr(next_item.payload, "\"cmd\":\"ac\""))) {
          xQueueReceive(s_mqtt_cmd_queue, &item, 0);
          continue;
        }
        break;
      }

      execute_gate_command(item.payload);

      if (strstr(item.payload, "hunonic_ac") || strstr(item.payload, "\"ac\"")) {
        vTaskDelay(pdMS_TO_TICKS(450));
      }
    }
  }
}

static void on_mqtt_command_received(const char *topic, int topic_len,
                                     const char *data, int data_len,
                                     void *arg) {
  if (!data || data_len <= 0 || !s_mqtt_cmd_queue)
    return;

  gate_mqtt_cmd_t item;
  int copy_len = (data_len < sizeof(item.payload) - 1) ? data_len : (sizeof(item.payload) - 1);
  memcpy(item.payload, data, copy_len);
  item.payload[copy_len] = '\0';

  if (xQueueSend(s_mqtt_cmd_queue, &item, 0) != pdTRUE) {
    ESP_LOGW(TAG, "Hang doi lenh MQTT bi day, bo qua lenh");
  }
}

esp_err_t gate_controller_init(void) {
  s_mqtt_cmd_queue = xQueueCreate(16, sizeof(gate_mqtt_cmd_t));
  if (!s_mqtt_cmd_queue) {
    ESP_LOGE(TAG, "Tao hang doi lenh MQTT that bai!");
    return ESP_ERR_NO_MEM;
  }
  xTaskCreate(gate_cmd_worker_task, "gate_cmd_worker", 12288, NULL, 4, NULL);

  esp_now_node_register_telemetry_cb(on_esp_now_telemetry);
  app_mqtt_register_data_cb(on_mqtt_command_received, NULL);
  hunonic_bridge_register_state_cb(on_hunonic_ac_state, NULL);
  ESP_LOGI(TAG, "Gate Controller da khoi tao thanh cong!");
  return ESP_OK;
}

void gate_controller_task(void *pvParam) {
  ESP_LOGI(TAG, "Gate Controller Heartbeat Task bat dau!");
  static uint32_t s_periodic_sync_counter = 0;
  static bool s_initial_sync_ready = false;

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    s_periodic_sync_counter += 5;

    if (!s_initial_sync_ready && hunonic_bridge_is_connected()) {
      s_initial_sync_ready = true;
      s_periodic_sync_counter = 0;
    }
    if (s_periodic_sync_counter >= 30) {
      s_periodic_sync_counter = 0;
      if (wifi_is_connected()) {
        esp_err_t sync_err = hunonic_bridge_sync_devices();
        int sc = 0;
        const hunonic_ac_device_t *sl = hunonic_bridge_get_devices(&sc);
        char *s_resp = sync_err == ESP_OK ? (char *)malloc(1536) : NULL;
        if (s_resp) {
          int s_len = snprintf(s_resp, 1536,
                             "{\"event\":\"device_list\","
                             "\"ac_count\":%d,\"devices\":[",
                             sc);
          bool s_first = true;
          for (int i = 0; i < sc; i++) {
            if (sl[i].is_valid) {
              s_len += snprintf(s_resp + s_len, 1536 - s_len,
                              "%s{\"id\":\"%s\",\"name\":\"%s\",\"power\":%d,"
                              "\"temp\":%d,\"mode\":%d,\"fan\":%d,\"brand\":%d}",
                              (!s_first ? "," : ""),
                              sl[i].id, sl[i].name, sl[i].power,
                              sl[i].temp, sl[i].mode, sl[i].fan,
                              sl[i].brand);
              s_first = false;
            }
          }
          snprintf(s_resp + s_len, 1536 - s_len, "]}");
#ifdef TOPIC_GATE_STATUS
          if (app_mqtt_is_connected()) {
            app_mqtt_publish(TOPIC_GATE_STATUS, s_resp, 0, 0);
          }
#endif
          free(s_resp);
        }
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
      int ac_m = (ac_count > 0 && ac_list && ac_list[0].is_valid)
                     ? ac_list[0].mode
                     : 2;
      int ac_f = (ac_count > 0 && ac_list && ac_list[0].is_valid)
                     ? ac_list[0].fan
                     : 0;
      int ac_b = (ac_count > 0 && ac_list && ac_list[0].is_valid)
                     ? ac_list[0].brand
                     : 0;
      const char *ac_id = (ac_count > 0 && ac_list && ac_list[0].is_valid)
                              ? ac_list[0].id
                              : "";
      const char *ac_name = (ac_count > 0 && ac_list && ac_list[0].is_valid)
                                ? ac_list[0].name
                                : "";

      bool ir_hub_on = hunonic_bridge_is_ir_hub_online();

      char status_buf[512];
      snprintf(
          status_buf, sizeof(status_buf),
          "{\"event\":\"heartbeat\",\"device\":\"esp32s3_gate\","
          "\"uptime_sec\":%lu,\"free_heap\":%lu,\"rssi\":%d,"
          "\"ac_count\":%d,\"ac_id\":\"%s\",\"ac_name\":\"%s\","
          "\"ac_brand\":%d,\"ac_power\":%d,\"ac_temp\":%d,\"ac_mode\":%d,\"ac_fan\":%d,"
          "\"ws_connected\":%d,\"ir_online\":%d,\"total_node_pkts\":%lu,\"fw_ver\":\"%s\"}",
          (unsigned long)uptime_sec, (unsigned long)free_heap, (int)rssi,
          ac_count, ac_id, ac_name, ac_b, ac_p,
          ac_t, ac_m, ac_f, hunonic_bridge_is_connected() ? 1 : 0,
          ir_hub_on ? 1 : 0,
          (unsigned long)total_packets, ota_get_current_version());

#ifdef TOPIC_GATE_STATUS
      if (app_mqtt_is_connected()) {
        app_mqtt_publish(TOPIC_GATE_STATUS, status_buf, 0, 0);
      }
#endif
    }
  }
}
