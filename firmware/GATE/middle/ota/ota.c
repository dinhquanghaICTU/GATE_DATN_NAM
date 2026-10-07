#include "ota.h"
#include "button.h"
#include "config.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jsmn.h"
#include "m_state_machine.h"
#include "mqtt.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "OTA_ENGINE";

static ota_config_t s_current_ota_cfg = {0};
static bool s_is_updating = false;
static char s_fw_ver_buf[32] = {0};

const char *ota_get_current_version(void) {
  if (s_fw_ver_buf[0] != '\0') {
    return s_fw_ver_buf;
  }
  nvs_handle_t nvs;
  if (nvs_open("gate_cfg", NVS_READONLY, &nvs) == ESP_OK) {
    size_t len = sizeof(s_fw_ver_buf);
    if (nvs_get_str(nvs, "fw_ver", s_fw_ver_buf, &len) == ESP_OK && len > 1) {
      nvs_close(nvs);
      return s_fw_ver_buf;
    }
    nvs_close(nvs);
  }
  const esp_app_desc_t *app_desc = esp_app_get_description();
  if (app_desc && strlen(app_desc->version) > 0) {
    strncpy(s_fw_ver_buf, app_desc->version, sizeof(s_fw_ver_buf) - 1);
  } else {
    strncpy(s_fw_ver_buf, "1.0.0", sizeof(s_fw_ver_buf) - 1);
  }
  return s_fw_ver_buf;
}

bool ota_is_updating(void) {
  return s_is_updating;
}

esp_err_t ota_parse_json(const char *json_str, ota_config_t *out_cfg) {
  if (!json_str || !out_cfg) {
    return ESP_ERR_INVALID_ARG;
  }
  memset(out_cfg, 0, sizeof(ota_config_t));

  jsmn_parser parser;
  jsmntok_t tokens[32];

  int r = json_parser(json_str, &parser, tokens, 32);
  if (r < 0) {
    ESP_LOGE(TAG, "Loi phan tich cu phap JSON bang JSMN!");
    return ESP_FAIL;
  }

  if (json_get_str(json_str, tokens, r, "url", out_cfg->url,
                   sizeof(out_cfg->url)) == 0) {
    ESP_LOGE(TAG, "Goi tin OTA thieu truong 'url'!");
    return ESP_ERR_INVALID_ARG;
  }

  json_get_str(json_str, tokens, r, "version", out_cfg->version,
               sizeof(out_cfg->version));
  json_get_str(json_str, tokens, r, "target", out_cfg->target,
               sizeof(out_cfg->target));
  json_get_str(json_str, tokens, r, "md5", out_cfg->md5, sizeof(out_cfg->md5));
  out_cfg->size = json_get_int(json_str, tokens, r, "size");

  ESP_LOGI(TAG, "Boc tach OTA thanh cong: Version=[%s], Target=[%s], Size=%d, URL=[%s]",
           out_cfg->version, out_cfg->target, out_cfg->size, out_cfg->url);

  return ESP_OK;
}

static void ota_task(void *pvParameter) {
  ESP_LOGI(TAG, "🚀 [OTA GATEWAY] Bat dau tai va nap tu: %s", s_current_ota_cfg.url);

  esp_http_client_config_t http_config = {
      .url = s_current_ota_cfg.url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 20000,
      .keep_alive_enable = true,
  };

  esp_https_ota_config_t ota_config = {
      .http_config = &http_config,
  };

  esp_https_ota_handle_t https_ota_handle = NULL;
  esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "❌ [OTA GATEWAY] Khoi tao OTA that bai: %s (0x%x)",
             esp_err_to_name(err), err);
    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf),
             "{\"event\":\"ota_progress\",\"target\":\"esp32s3_gate\","
             "\"status\":\"failed\",\"percent\":0,\"error\":\"%s\"}",
             esp_err_to_name(err));
#ifdef TOPIC_GATE_STATUS
    app_mqtt_publish(TOPIC_GATE_STATUS, err_buf, 1, 0);
#endif
    button_task_start();
    m_state_machine_set_state(STATE_RUNNING);
    s_is_updating = false;
    vTaskDelete(NULL);
    return;
  }

  int image_size = esp_https_ota_get_image_size(https_ota_handle);
  if (image_size <= 0) {
    image_size = s_current_ota_cfg.size;
  }

  int last_reported_percent = -1;

  while (1) {
    err = esp_https_ota_perform(https_ota_handle);
    if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
      break;
    }
    int read_len = esp_https_ota_get_image_len_read(https_ota_handle);
    int percent = (image_size > 0) ? (read_len * 100 / image_size) : 0;
    if (percent > 100) percent = 100;

    if (percent != last_reported_percent && (percent % 10 == 0 || last_reported_percent == -1)) {
      last_reported_percent = percent;
      ESP_LOGI(TAG, "📡 [OTA S3 PROGRESS] %d%% (%d/%d bytes)", percent, read_len, image_size);
      char prog_buf[256];
      snprintf(prog_buf, sizeof(prog_buf),
               "{\"event\":\"ota_progress\",\"target\":\"esp32s3_gate\","
               "\"status\":\"in_progress\",\"percent\":%d,\"bytes\":%d,\"total\":%d}",
               percent, read_len, image_size);
#ifdef TOPIC_GATE_STATUS
      app_mqtt_publish(TOPIC_GATE_STATUS, prog_buf, 1, 0);
#endif
    }
  }

  if (esp_https_ota_is_complete_data_received(https_ota_handle)) {
    esp_err_t finish_err = esp_https_ota_finish(https_ota_handle);
    if (finish_err == ESP_OK) {
      ESP_LOGI(TAG, "🎉 [OTA GATEWAY] CAP NHAT THANH CONG 100%%!");
      if (strlen(s_current_ota_cfg.version) > 0) {
        nvs_handle_t nvs;
        if (nvs_open("gate_cfg", NVS_READWRITE, &nvs) == ESP_OK) {
          nvs_set_str(nvs, "fw_pending", s_current_ota_cfg.version);
          nvs_commit(nvs);
          nvs_close(nvs);
          ESP_LOGI(TAG, "💾 Da luu fw_pending: [%s]", s_current_ota_cfg.version);
        }
      }
      char stat_buf[256];
      snprintf(stat_buf, sizeof(stat_buf),
               "{\"event\":\"ota_progress\",\"target\":\"esp32s3_gate\","
               "\"status\":\"rebooting\",\"percent\":100,\"message\":\"Da ghi flash 100%%, rebooting...\"}");
#ifdef TOPIC_GATE_STATUS
      app_mqtt_publish(TOPIC_GATE_STATUS, stat_buf, 1, 0);
#endif
      vTaskDelay(pdMS_TO_TICKS(500));
      ESP_LOGI(TAG, "Khoi dong lai chip sau 2 giay...");
      vTaskDelay(pdMS_TO_TICKS(2000));
      esp_restart();
    } else {
      ESP_LOGE(TAG, "❌ [OTA GATEWAY] Finish that bai: %s (0x%x)",
               esp_err_to_name(finish_err), finish_err);
    }
  } else {
    esp_https_ota_abort(https_ota_handle);
    ESP_LOGE(TAG, "❌ [OTA GATEWAY] Nap that bai giua chung: %s", esp_err_to_name(err));
  }

  button_task_start();
  m_state_machine_set_state(STATE_RUNNING);
  s_is_updating = false;
  vTaskDelete(NULL);
}

esp_err_t ota_start(const ota_config_t *config) {
  if (!config || strlen(config->url) == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_is_updating) {
    ESP_LOGW(TAG, "He thong dang trong qua trinh OTA, bo qua yeu cau moi!");
    return ESP_ERR_INVALID_STATE;
  }

  s_is_updating = true;
  memcpy(&s_current_ota_cfg, config, sizeof(ota_config_t));

  m_state_machine_set_state(STATE_OTA);
  button_task_stop();

  BaseType_t res = xTaskCreate(ota_task, "ota_task", 8192, NULL, 5, NULL);
  if (res != pdPASS) {
    s_is_updating = false;
    button_task_start();
    m_state_machine_set_state(STATE_RUNNING);
    return ESP_FAIL;
  }

  return ESP_OK;
}
