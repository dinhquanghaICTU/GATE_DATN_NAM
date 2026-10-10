#include "hunonic_bridge.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "flash.h"
#include "hunonic_crypto.h"
#include "mbedtls/base64.h"
#include "mbedtls/md5.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "HUNONIC_BRIDGE";

#define HUNONIC_API_LOGIN_URL "https://web.hunonic.com/api/api/auth/login"
#define HUNONIC_API_DEVICE_LIST_URL                                            \
  "https://web.hunonic.com/api/api/hun-api/device/listDeviceByHome"
#define HUNONIC_WS_GATEWAY_URL "wss://web.hunonic.com/gateway/ws?token="

#define HTTP_RESP_BUFFER_SIZE 16384
#define TOKEN_MAX_LEN 512
#define PHONE_MAX_LEN 32
#define PASS_MAX_LEN 64

static char s_token[TOKEN_MAX_LEN] = {0};
static char s_user_id[32] = {0};
static char s_phone[PHONE_MAX_LEN] = {0};
static char s_password[PASS_MAX_LEN] = {0};
static char s_ir_gateway_root_id[128] = {0};
static char s_ir_gateway_topic_sub[128] = {0};
static char s_ir_gateway_topic_pub[128] = {0};

static void hunonic_ws_subscribe_all(void);
static esp_err_t hunonic_ws_start(void);

static hunonic_ac_device_t s_ac_devices[MAX_HUNONIC_AC_DEVICES];
static int s_ac_count = 0;

static esp_websocket_client_handle_t s_ws_client = NULL;
static bool s_is_ws_connected = false;

static hunonic_ac_state_cb_t s_state_cb = NULL;
static void *s_state_cb_arg = NULL;

static void compute_md5(const char *input, char *output_32) {
  uint8_t digest[16];
  mbedtls_md5((const unsigned char *)input, strlen(input), digest);
  for (int i = 0; i < 16; i++) {
    sprintf(output_32 + (i * 2), "%02x", digest[i]);
  }
  output_32[32] = '\0';
}

static void parse_user_id_from_jwt(const char *token) {
  if (!token)
    return;
  const char *dot1 = strchr(token, '.');
  if (!dot1)
    return;
  const char *dot2 = strchr(dot1 + 1, '.');
  if (!dot2)
    return;

  size_t b64_len = dot2 - (dot1 + 1);
  char b64_buf[256] = {0};
  if (b64_len >= sizeof(b64_buf))
    return;
  memcpy(b64_buf, dot1 + 1, b64_len);

  while (strlen(b64_buf) % 4 != 0) {
    strcat(b64_buf, "=");
  }

  uint8_t payload_bytes[256] = {0};
  size_t olen = 0;
  if (mbedtls_base64_decode(payload_bytes, sizeof(payload_bytes) - 1, &olen,
                            (const unsigned char *)b64_buf,
                            strlen(b64_buf)) == 0) {
    payload_bytes[olen] = '\0';
    cJSON *jwt_json = cJSON_Parse((char *)payload_bytes);
    if (jwt_json) {
      cJSON *id_item = cJSON_GetObjectItem(jwt_json, "id");
      if (id_item && id_item->valuestring) {
        strncpy(s_user_id, id_item->valuestring, sizeof(s_user_id) - 1);
        ESP_LOGI(TAG, "Da trich xuat user_id tu JWT: %s", s_user_id);
      }
      cJSON_Delete(jwt_json);
    }
  }
}

typedef struct {
  char *buffer;
  int buffer_len;
} http_response_buffer_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  http_response_buffer_t *resp = (http_response_buffer_t *)evt->user_data;
  if (evt->event_id == HTTP_EVENT_ON_DATA) {
    if (resp && resp->buffer) {
      if (resp->buffer_len + evt->data_len < HTTP_RESP_BUFFER_SIZE - 1) {
        memcpy(resp->buffer + resp->buffer_len, evt->data, evt->data_len);
        resp->buffer_len += evt->data_len;
        resp->buffer[resp->buffer_len] = '\0';
      } else {
        ESP_LOGE(TAG, "HTTP Response vuot qua kich thuoc buffer (%d bytes)",
                 HTTP_RESP_BUFFER_SIZE);
      }
    }
  }
  return ESP_OK;
}

static esp_err_t hunonic_http_login(const char *phone, const char *password) {
  if (!phone || !password)
    return ESP_ERR_INVALID_ARG;

  char pass_md5[33] = {0};
  if (strlen(password) == 32) {
    strncpy(pass_md5, password, sizeof(pass_md5) - 1);
  } else {
    compute_md5(password, pass_md5);
  }

  char post_data[128];
  snprintf(post_data, sizeof(post_data),
           "{\"phone\":\"%s\",\"password\":\"%s\"}", phone, pass_md5);

  char *resp_buf = (char *)malloc(HTTP_RESP_BUFFER_SIZE);
  if (!resp_buf)
    return ESP_ERR_NO_MEM;
  memset(resp_buf, 0, HTTP_RESP_BUFFER_SIZE);

  http_response_buffer_t resp = {.buffer = resp_buf, .buffer_len = 0};

  esp_http_client_config_t config = {
      .url = HUNONIC_API_LOGIN_URL,
      .method = HTTP_METHOD_POST,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .event_handler = http_event_handler,
      .user_data = &resp,
      .timeout_ms = 10000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "Accept", "application/json");
  esp_http_client_set_post_field(client, post_data, strlen(post_data));

  ESP_LOGI(TAG, "Dang goi API Login Hunonic cho SĐT: %s ...", phone);
  esp_err_t err = esp_http_client_perform(client);
  int status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK || status_code != 200) {
    ESP_LOGE(TAG, "Login that bai: err=%d, HTTP status=%d", err, status_code);
    free(resp_buf);
    return ESP_FAIL;
  }

  cJSON *root = cJSON_Parse(resp_buf);
  free(resp_buf);
  if (!root) {
    ESP_LOGE(TAG, "Parse JSON phan hoi login that bai");
    return ESP_FAIL;
  }

  const char *token_str = NULL;
  cJSON *data_item = cJSON_GetObjectItem(root, "data");
  if (data_item) {
    if (cJSON_IsString(data_item) && data_item->valuestring) {
      token_str = data_item->valuestring;
    } else if (cJSON_IsObject(data_item)) {
      cJSON *tok_obj = cJSON_GetObjectItem(data_item, "token");
      if (tok_obj && tok_obj->valuestring) {
        token_str = tok_obj->valuestring;
      }
    }
  }

  if (!token_str || strlen(token_str) == 0) {
    cJSON *msg_item = cJSON_GetObjectItem(root, "message");
    ESP_LOGE(TAG, "Dang nhap that bai hoac khong tim thay token: %s",
             msg_item && msg_item->valuestring ? msg_item->valuestring
                                               : "Unknown");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  strncpy(s_token, token_str, sizeof(s_token) - 1);
  cJSON_Delete(root);

  ESP_LOGI(TAG, "Dang nhap thanh cong! Token da nhan (do dai: %d)",
           (int)strlen(s_token));

  nvs_save_string("hun_token", s_token);
  nvs_save_string("hun_phone", phone);
  nvs_save_string("hun_pass", pass_md5);

  parse_user_id_from_jwt(s_token);
  return ESP_OK;
}

esp_err_t hunonic_bridge_sync_devices(void) {
  if (strlen(s_token) == 0) {
    ESP_LOGW(TAG, "Chua co token, khong the sync thiet bi");
    return ESP_ERR_INVALID_STATE;
  }

  char *resp_buf = NULL;
  for (int attempt = 0; attempt < 2; attempt++) {
    resp_buf = (char *)malloc(HTTP_RESP_BUFFER_SIZE);
    if (!resp_buf)
      return ESP_ERR_NO_MEM;
    memset(resp_buf, 0, HTTP_RESP_BUFFER_SIZE);

    http_response_buffer_t resp = {.buffer = resp_buf, .buffer_len = 0};

    esp_http_client_config_t config = {
        .url = HUNONIC_API_DEVICE_LIST_URL,
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    char auth_header[TOKEN_MAX_LEN + 16];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Accept", "application/json");

    ESP_LOGI(TAG, "Dang dong bo danh sach thiet bi tu Hunonic Cloud (lan %d)...", attempt + 1);
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status_code != 200) {
      ESP_LOGE(TAG, "Sync thiet bi that bai: err=%d, HTTP status=%d", err,
               status_code);
      if (resp_buf && strlen(resp_buf) > 0) {
        ESP_LOGE(TAG, "Phan hoi tu server: %s", resp_buf);
      }
      free(resp_buf);
      resp_buf = NULL;

      if (attempt == 0 &&
          (status_code == 401 || status_code == 403 || status_code == 500 ||
           status_code == 400) &&
          strlen(s_phone) > 0 && strlen(s_password) > 0) {
        ESP_LOGW(
            TAG,
            "Phat hien Token bi mat quyen/thiet bi khac dang nhap (HTTP %d)!",
            status_code);
        ESP_LOGW(TAG, "Gate dang tu dong dang nhap lai de lay Token chu quyen "
                      "& da thiet bi khac ra...");

        if (hunonic_http_login(s_phone, s_password) == ESP_OK) {
          ESP_LOGI(TAG, "Re-Login thanh cong! Khoi dong lai WebSocket voi "
                        "Token chu quyen moi...");
          hunonic_ws_start();
          continue;
        }
      }
      return ESP_FAIL;
    }

    break; // Sync HTTP thanh cong -> tiep tuc xu ly JSON
  }

  if (!resp_buf) {
    return ESP_FAIL;
  }

  cJSON *root = cJSON_Parse(resp_buf);
  free(resp_buf);
  if (!root) {
    ESP_LOGE(TAG, "Parse JSON listDeviceByHome that bai");
    return ESP_FAIL;
  }

  s_ac_count = 0;
  memset(s_ac_devices, 0, sizeof(s_ac_devices));

  cJSON *data_arr = cJSON_GetObjectItem(root, "data");
  if (data_arr && cJSON_IsArray(data_arr)) {
    cJSON *home = NULL;
    cJSON_ArrayForEach(home, data_arr) {
      cJSON *uid = cJSON_GetObjectItem(home, "user_id");
      if (uid && uid->valuestring && strlen(s_user_id) == 0) {
        strncpy(s_user_id, uid->valuestring, sizeof(s_user_id) - 1);
      }

      cJSON *rooms = cJSON_GetObjectItem(home, "rooms");
      if (rooms && cJSON_IsArray(rooms)) {
        cJSON *room = NULL;
        cJSON_ArrayForEach(room, rooms) {
          cJSON *devs = cJSON_GetObjectItem(room, "devices");
          if (devs && cJSON_IsArray(devs)) {
            cJSON *dev = NULL;
            cJSON_ArrayForEach(dev, devs) {
              cJSON *type = cJSON_GetObjectItem(dev, "type");
              cJSON *root_type = cJSON_GetObjectItem(dev, "root_type");
              cJSON *root_id = cJSON_GetObjectItem(dev, "root_id");

              if (type && type->valuestring &&
                  (strcmp(type->valuestring, "irwifi") == 0 ||
                   (root_type &&
                    strcmp(root_type->valuestring, "irwifiv2") == 0))) {
                if (root_id && root_id->valuestring) {
                  strncpy(s_ir_gateway_root_id, root_id->valuestring,
                          sizeof(s_ir_gateway_root_id) - 1);
                  ESP_LOGI(TAG, "Phat hien Tram IR Hub: root_id=%s",
                           s_ir_gateway_root_id);
                }
                cJSON *hub_sub = cJSON_GetObjectItem(dev, "topicsub");
                cJSON *hub_pub = cJSON_GetObjectItem(dev, "topicpub");
                if (hub_sub && hub_sub->valuestring) {
                  strncpy(s_ir_gateway_topic_sub, hub_sub->valuestring,
                          sizeof(s_ir_gateway_topic_sub) - 1);
                }
                if (hub_pub && hub_pub->valuestring) {
                  strncpy(s_ir_gateway_topic_pub, hub_pub->valuestring,
                          sizeof(s_ir_gateway_topic_pub) - 1);
                }
              }

              cJSON *cat = cJSON_GetObjectItem(dev, "category");
              cJSON *cat_name =
                  cat ? cJSON_GetObjectItem(cat, "name_en") : NULL;
              bool is_ac = false;
              if (cat_name && cat_name->valuestring &&
                  strcmp(cat_name->valuestring, "AC") == 0) {
                is_ac = true;
              } else if (type && type->valuestring &&
                         strcmp(type->valuestring, "irchild") == 0) {
                is_ac = true;
              }

              if (is_ac && s_ac_count < MAX_HUNONIC_AC_DEVICES) {
                hunonic_ac_device_t *ac = &s_ac_devices[s_ac_count];
                cJSON *id = cJSON_GetObjectItem(dev, "id");
                cJSON *name = cJSON_GetObjectItem(dev, "name");
                cJSON *t_sub = cJSON_GetObjectItem(dev, "topicsub");
                cJSON *t_pub = cJSON_GetObjectItem(dev, "topicpub");
                cJSON *val = cJSON_GetObjectItem(dev, "value");
                cJSON *brand_obj = cJSON_GetObjectItem(dev, "brand");
                cJSON *brand_id =
                    brand_obj ? cJSON_GetObjectItem(brand_obj, "id") : NULL;

                if (id && id->valuestring)
                  strncpy(ac->id, id->valuestring, sizeof(ac->id) - 1);
                if (name && name->valuestring)
                  strncpy(ac->name, name->valuestring, sizeof(ac->name) - 1);
                if (t_sub && t_sub->valuestring)
                  strncpy(ac->topic_sub, t_sub->valuestring,
                          sizeof(ac->topic_sub) - 1);
                if (t_pub && t_pub->valuestring)
                  strncpy(ac->topic_pub, t_pub->valuestring,
                          sizeof(ac->topic_pub) - 1);
                if (brand_id && brand_id->valuestring)
                  ac->brand = atoi(brand_id->valuestring);

                bool state_changed = false;
                if (val && val->valuestring) {
                  cJSON *v_json = cJSON_Parse(val->valuestring);
                  if (v_json) {
                    cJSON *p = cJSON_GetObjectItem(v_json, "power");
                    cJSON *t = cJSON_GetObjectItem(v_json, "temp");
                    cJSON *m = cJSON_GetObjectItem(v_json, "mode");
                    cJSON *f = cJSON_GetObjectItem(v_json, "fan");
                    cJSON *b = cJSON_GetObjectItem(v_json, "brand");
                    if (p && ac->power != p->valueint) {
                      ac->power = p->valueint;
                      state_changed = true;
                    }
                    if (t && ac->temp != t->valueint) {
                      ac->temp = t->valueint;
                      state_changed = true;
                    }
                    if (m && ac->mode != m->valueint) {
                      ac->mode = m->valueint;
                      state_changed = true;
                    }
                    if (f && ac->fan != f->valueint) {
                      ac->fan = f->valueint;
                      state_changed = true;
                    }
                    if (b && ac->brand == 0)
                      ac->brand = b->valueint;
                    cJSON_Delete(v_json);
                  }
                }

                ac->is_valid = true;
                ESP_LOGI(TAG,
                         "-> Phat hien Dieu hoa: [%s] '%s' (Brand %d) | "
                         "Power=%d, Temp=%d, Mode=%d, Fan=%d",
                         ac->id, ac->name, ac->brand, ac->power, ac->temp,
                         ac->mode, ac->fan);

                if (state_changed && s_state_cb) {
                  ESP_LOGI(TAG, "🔄 [HTTP SYNC DETECT] Trang thai thay doi -> "
                                "Trigger State CB!");
                  s_state_cb(ac, s_state_cb_arg);
                }

                s_ac_count++;
              }
            }
          }
        }
      }
    }
  }

  cJSON_Delete(root);
  ESP_LOGI(TAG, "Dong bo hoan tat: tim thay %d dieu hoa", s_ac_count);

  if (s_is_ws_connected) {
    hunonic_ws_subscribe_all();
  }
  return ESP_OK;
}

static char *s_ws_rx_buf = NULL;
static int s_ws_rx_len = 0;

static esp_err_t hunonic_ws_subscribe(const char *topic) {
  if (!s_is_ws_connected || !s_ws_client || !topic || strlen(topic) == 0) {
    return ESP_ERR_INVALID_STATE;
  }

  int64_t now_ms = (int64_t)(esp_timer_get_time() / 1000);

  cJSON *sub_obj = cJSON_CreateObject();
  cJSON_AddStringToObject(sub_obj, "topic", topic);
  cJSON_AddStringToObject(sub_obj, "type", "sub");

  char *sub_str = cJSON_PrintUnformatted(sub_obj);
  cJSON_Delete(sub_obj);

  if (!sub_str)
    return ESP_ERR_NO_MEM;

  cJSON *envelope = cJSON_CreateObject();
  cJSON_AddStringToObject(envelope, "data", sub_str);
  cJSON_AddNumberToObject(envelope, "ts", (double)now_ms);
  free(sub_str);

  char *plaintext = cJSON_PrintUnformatted(envelope);
  cJSON_Delete(envelope);

  if (!plaintext)
    return ESP_ERR_NO_MEM;

  char *enc_b64 = (char *)malloc(1024);
  if (!enc_b64) {
    free(plaintext);
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = hunonic_crypto_encrypt(plaintext, enc_b64, 1024);
  free(plaintext);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Ma hoa subscribe topic [%s] that bai: %d", topic, err);
    free(enc_b64);
    return err;
  }

  char *ws_frame = (char *)malloc(1200);
  if (!ws_frame) {
    free(enc_b64);
    return ESP_ERR_NO_MEM;
  }
  snprintf(ws_frame, 1200, "{\"data\":\"%s\"}", enc_b64);
  free(enc_b64);

  ESP_LOGI(TAG, "📡 -> [WS SUBSCRIBE] Topic: %s", topic);
  int sent = esp_websocket_client_send_text(
      s_ws_client, ws_frame, strlen(ws_frame), pdMS_TO_TICKS(2000));
  free(ws_frame);

  return (sent >= 0) ? ESP_OK : ESP_FAIL;
}

static void hunonic_ws_subscribe_all(void) {
  if (!s_is_ws_connected || !s_ws_client) {
    return;
  }
  ESP_LOGI(TAG, "=== Bat dau Subscribe cac topic Hunonic Cloud ===");

  // 1. Subscribe topic cua cac thiet bi dieu hoa
  for (int i = 0; i < s_ac_count; i++) {
    if (s_ac_devices[i].is_valid) {
      if (strlen(s_ac_devices[i].topic_sub) > 0) {
        hunonic_ws_subscribe(s_ac_devices[i].topic_sub);
        vTaskDelay(pdMS_TO_TICKS(40));
      }
      if (strlen(s_ac_devices[i].topic_pub) > 0) {
        hunonic_ws_subscribe(s_ac_devices[i].topic_pub);
        vTaskDelay(pdMS_TO_TICKS(40));
      }
    }
  }

  // 2. Subscribe topic cua tram IR Hub cha (neu co)
  if (strlen(s_ir_gateway_topic_sub) > 0) {
    hunonic_ws_subscribe(s_ir_gateway_topic_sub);
    vTaskDelay(pdMS_TO_TICKS(40));
  }
  if (strlen(s_ir_gateway_topic_pub) > 0) {
    hunonic_ws_subscribe(s_ir_gateway_topic_pub);
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

static void update_ac_state_and_notify(const char *device_id, int brand,
                                       int power, int temp, int mode, int fan,
                                       const char *source_event) {
  hunonic_ac_device_t *target = NULL;

  // 1. Uu tien tim theo device_id neu co
  if (device_id && strlen(device_id) > 0) {
    for (int i = 0; i < s_ac_count; i++) {
      if (s_ac_devices[i].is_valid &&
          strcmp(s_ac_devices[i].id, device_id) == 0) {
        target = &s_ac_devices[i];
        break;
      }
    }
  }

  // 2. Neu thong diep khong co device_id, tim theo ma Brand (vi du 104 = Funiki, 117 = Mirage)
  if (!target && brand > 0) {
    for (int i = 0; i < s_ac_count; i++) {
      if (s_ac_devices[i].is_valid && s_ac_devices[i].brand == brand) {
        target = &s_ac_devices[i];
        break;
      }
    }
  }

  // 3. Neu khong tim thay theo ID va Brand, nhung chi co duy nhat 1 dieu hoa
  if (!target && s_ac_count == 1 && s_ac_devices[0].is_valid) {
    target = &s_ac_devices[0];
  }

  if (!target) {
    ESP_LOGW(TAG, "Khong tim thay thiet bi dieu hoa phu hop (ID='%s', Brand=%d) de cap nhat!",
             device_id ? device_id : "", brand);
    return;
  }

  bool changed = false;
  if (power >= 0 && target->power != power) {
    target->power = power;
    changed = true;
  }
  if (temp > 0 && target->temp != temp) {
    target->temp = temp;
    changed = true;
  }
  if (mode >= 0 && target->mode != mode) {
    target->mode = mode;
    changed = true;
  }
  if (fan >= 0 && target->fan != fan) {
    target->fan = fan;
    changed = true;
  }

  ESP_LOGI(TAG,
           "🎯 [2-WAY SYNC OK] Dieu hoa [%s] '%s' -> Nguon: %s | "
           "Power=%d, Temp=%d, Mode=%d, Fan=%d (Thay doi: %s)",
           target->id, target->name, source_event ? source_event : "UNKNOWN",
           target->power, target->temp, target->mode, target->fan,
           changed ? "CO" : "KHONG");

  if (changed && s_state_cb) {
    s_state_cb(target, s_state_cb_arg);
  }
}

static void parse_ws_device_message(cJSON *msg) {
  if (!msg || !cJSON_IsObject(msg))
    return;

  cJSON *evt = cJSON_GetObjectItem(msg, "event");
  const char *evt_str = (evt && evt->valuestring) ? evt->valuestring : "";

  cJSON *id_item = cJSON_GetObjectItem(msg, "id");
  const char *dev_id =
      (id_item && id_item->valuestring) ? id_item->valuestring : "";

  int power = -1, temp = -1, mode = -1, fan = -1, brand = -1;
  bool found_state = false;

  // 1. Truong hop AC_STATE (IR Hub phan hoi ket qua sau khi ban IR)
  if (strcmp(evt_str, "AC_STATE") == 0) {
    cJSON *meta = cJSON_GetObjectItem(msg, "metaData");
    if (meta) {
      cJSON *st = NULL;
      if (cJSON_IsString(meta) && meta->valuestring) {
        st = cJSON_Parse(meta->valuestring);
      } else if (cJSON_IsObject(meta)) {
        st = meta;
      }

      if (st) {
        cJSON *b = cJSON_GetObjectItem(st, "brand");
        cJSON *p = cJSON_GetObjectItem(st, "power");
        cJSON *t = cJSON_GetObjectItem(st, "temp");
        cJSON *m = cJSON_GetObjectItem(st, "mode");
        cJSON *f = cJSON_GetObjectItem(st, "fan");

        if (b)
          brand = b->valueint;
        if (p)
          power = p->valueint;
        if (t)
          temp = t->valueint;
        if (m)
          mode = m->valueint;
        if (f)
          fan = f->valueint;
        found_state = true;

        if (st != meta)
          cJSON_Delete(st);
      }
    }
  }
  // 2. Truong hop AC_CONTROL (App Hunonic hoac client khac dieu khien)
  else if (strcmp(evt_str, "AC_CONTROL") == 0) {
    cJSON *action = cJSON_GetObjectItem(msg, "action");
    if (action) {
      cJSON *act = NULL;
      if (cJSON_IsString(action) && action->valuestring) {
        act = cJSON_Parse(action->valuestring);
      } else if (cJSON_IsObject(action)) {
        act = action;
      }

      if (act) {
        cJSON *b = cJSON_GetObjectItem(act, "brand");
        cJSON *p = cJSON_GetObjectItem(act, "power");
        cJSON *t = cJSON_GetObjectItem(act, "temp");
        cJSON *m = cJSON_GetObjectItem(act, "mode");
        cJSON *f = cJSON_GetObjectItem(act, "fan");

        if (b)
          brand = b->valueint;
        if (p)
          power = p->valueint;
        if (t)
          temp = t->valueint;
        if (m)
          mode = m->valueint;
        if (f)
          fan = f->valueint;
        found_state = true;

        if (act != action)
          cJSON_Delete(act);
      }
    }
  }

  // 3. Truong hop thong diep truc tiep co power / action
  if (!found_state) {
    cJSON *b = cJSON_GetObjectItem(msg, "brand");
    cJSON *p = cJSON_GetObjectItem(msg, "power");
    cJSON *t = cJSON_GetObjectItem(msg, "temp");
    cJSON *m = cJSON_GetObjectItem(msg, "mode");
    cJSON *f = cJSON_GetObjectItem(msg, "fan");
    if (b)
      brand = b->valueint;
    if (p || t) {
      if (p)
        power = p->valueint;
      if (t)
        temp = t->valueint;
      if (m)
        mode = m->valueint;
      if (f)
        fan = f->valueint;
      found_state = true;
    }
  }

  if (found_state) {
    update_ac_state_and_notify(dev_id, brand, power, temp, mode, fan, evt_str);
  }
}

static void unpack_and_dispatch(cJSON *obj) {
  if (!obj)
    return;

  // Neu obj co payload (dang pubsub {topic:..., payload:...})
  cJSON *payload = cJSON_GetObjectItem(obj, "payload");
  if (payload) {
    if (cJSON_IsString(payload) && payload->valuestring) {
      cJSON *p_json = cJSON_Parse(payload->valuestring);
      if (p_json) {
        unpack_and_dispatch(p_json);
        cJSON_Delete(p_json);
      }
    } else if (cJSON_IsObject(payload)) {
      unpack_and_dispatch(payload);
    }
    return;
  }

  // Neu obj co data (envelope {data:..., ts:...})
  cJSON *d = cJSON_GetObjectItem(obj, "data");
  if (d) {
    if (cJSON_IsString(d) && d->valuestring) {
      cJSON *d_json = cJSON_Parse(d->valuestring);
      if (d_json) {
        unpack_and_dispatch(d_json);
        cJSON_Delete(d_json);
      }
    } else if (cJSON_IsObject(d)) {
      unpack_and_dispatch(d);
    }
    return;
  }

  // Neu obj la message cuoi cung
  parse_ws_device_message(obj);
}

static void handle_ws_incoming_payload(const char *decrypted_json) {
  if (!decrypted_json || strlen(decrypted_json) == 0)
    return;

  ESP_LOGI(TAG, "<- [WS INCOMING DECRYPTED]: %s", decrypted_json);

  cJSON *root = cJSON_Parse(decrypted_json);
  if (!root) {
    ESP_LOGW(TAG, "Khong the parse JSON decrypted");
    return;
  }

  if (cJSON_IsArray(root)) {
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
      if (cJSON_IsString(item) && item->valuestring) {
        cJSON *sub_json = cJSON_Parse(item->valuestring);
        if (sub_json) {
          unpack_and_dispatch(sub_json);
          cJSON_Delete(sub_json);
        }
      } else if (cJSON_IsObject(item)) {
        unpack_and_dispatch(item);
      }
    }
  } else if (cJSON_IsObject(root)) {
    unpack_and_dispatch(root);
  }

  cJSON_Delete(root);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data) {
  esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
  switch (event_id) {
  case WEBSOCKET_EVENT_CONNECTED:
    ESP_LOGI(TAG, "✅ WebSocket Hunonic DA KET NOI THANH CONG!");
    s_is_ws_connected = true;
    hunonic_ws_subscribe_all();
    break;

  case WEBSOCKET_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "⚠️ WebSocket Hunonic bi mat ket noi!");
    s_is_ws_connected = false;
    if (s_ws_rx_buf) {
      free(s_ws_rx_buf);
      s_ws_rx_buf = NULL;
      s_ws_rx_len = 0;
    }
    break;

  case WEBSOCKET_EVENT_DATA:
    if (data->data_len > 0) {
      // Xu ly ghep frame WebSocket phong truong hop bi phan manh
      if (data->payload_offset == 0) {
        if (s_ws_rx_buf)
          free(s_ws_rx_buf);
        s_ws_rx_buf = (char *)malloc(data->payload_len + 1);
        s_ws_rx_len = 0;
      }

      if (s_ws_rx_buf && (s_ws_rx_len + data->data_len <= data->payload_len)) {
        memcpy(s_ws_rx_buf + data->payload_offset, data->data_ptr,
               data->data_len);
        s_ws_rx_len += data->data_len;
        s_ws_rx_buf[s_ws_rx_len] = '\0';
      }

      // Khi da nhan du toan bo frame
      if (s_ws_rx_buf && (s_ws_rx_len >= data->payload_len)) {
        cJSON *root = cJSON_Parse(s_ws_rx_buf);
        if (root) {
          cJSON *d_item = cJSON_GetObjectItem(root, "data");
          if (d_item && d_item->valuestring) {
            char *dec_buf = (char *)malloc(4096);
            if (dec_buf) {
              if (hunonic_crypto_decrypt(d_item->valuestring, dec_buf, 4096) ==
                  ESP_OK) {
                handle_ws_incoming_payload(dec_buf);
              } else {
                ESP_LOGE(TAG, "Giai ma frame WebSocket Hunonic that bai!");
              }
              free(dec_buf);
            }
          }
          cJSON_Delete(root);
        }

        free(s_ws_rx_buf);
        s_ws_rx_buf = NULL;
        s_ws_rx_len = 0;
      }
    }
    break;

  case WEBSOCKET_EVENT_ERROR:
    ESP_LOGE(TAG, "WebSocket Hunonic bao loi!");
    break;
  }
}

static esp_err_t hunonic_ws_start(void) {
  if (strlen(s_token) == 0)
    return ESP_ERR_INVALID_STATE;

  char ws_uri[TOKEN_MAX_LEN + 64];
  snprintf(ws_uri, sizeof(ws_uri), "%s%s", HUNONIC_WS_GATEWAY_URL, s_token);

  esp_websocket_client_config_t ws_cfg = {
      .uri = ws_uri,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .reconnect_timeout_ms = 5000,
      .network_timeout_ms = 10000,
      .task_stack = 10240,
  };

  if (s_ws_client) {
    esp_websocket_client_stop(s_ws_client);
    esp_websocket_client_destroy(s_ws_client);
  }

  s_ws_client = esp_websocket_client_init(&ws_cfg);
  esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY,
                                websocket_event_handler, NULL);
  return esp_websocket_client_start(s_ws_client);
}

esp_err_t hunonic_bridge_control(const char *device_id, int power, int temp,
                                 int mode, int fan) {
  if (!s_is_ws_connected || !s_ws_client) {
    ESP_LOGW(TAG, "WebSocket dang ket noi/khoi tao lai, cho toi da 2.5s...");
    for (int retry = 0; retry < 25; retry++) {
      vTaskDelay(pdMS_TO_TICKS(100));
      if (s_is_ws_connected && s_ws_client) {
        break;
      }
    }
  }

  if (!s_is_ws_connected || !s_ws_client) {
    ESP_LOGE(TAG, "WebSocket chua ket noi, khong the gui lenh");
    return ESP_ERR_INVALID_STATE;
  }

  hunonic_ac_device_t *target = NULL;
  for (int i = 0; i < s_ac_count; i++) {
    if (s_ac_devices[i].is_valid &&
        strcmp(s_ac_devices[i].id, device_id) == 0) {
      target = &s_ac_devices[i];
      break;
    }
  }

  if (!target) {
    ESP_LOGE(TAG, "Khong tim thay dieu hoa voi ID: %s", device_id);
    return ESP_ERR_NOT_FOUND;
  }

  int64_t now_ms = (int64_t)(esp_timer_get_time() / 1000);
  const char *r_id = s_ir_gateway_root_id[0] ? s_ir_gateway_root_id : "";

  cJSON *action = cJSON_CreateObject();
  cJSON_AddNumberToObject(action, "irwifiv2", 1);
  cJSON_AddNumberToObject(action, "type", 1);
  cJSON_AddNumberToObject(action, "brand", target->brand);
  cJSON_AddNumberToObject(action, "power", power);
  cJSON_AddNumberToObject(action, "temp", temp);
  cJSON_AddNumberToObject(action, "mode", mode);
  cJSON_AddNumberToObject(action, "fan", fan);
  cJSON_AddNumberToObject(action, "act", 0);
  cJSON_AddStringToObject(action, "u", s_user_id);

  cJSON *cmd = cJSON_CreateObject();
  cJSON_AddStringToObject(cmd, "id", target->id);
  cJSON_AddStringToObject(cmd, "type", "irwifiv2");
  cJSON_AddStringToObject(cmd, "root_type", "irwifiv2");
  cJSON_AddStringToObject(cmd, "topicSub", target->topic_sub);
  cJSON_AddStringToObject(cmd, "topicPub", target->topic_pub);
  cJSON_AddStringToObject(cmd, "event", "AC_CONTROL");
  cJSON_AddStringToObject(cmd, "root_id", r_id);
  cJSON_AddItemToObject(cmd, "action", action);

  char *cmd_str = cJSON_PrintUnformatted(cmd);
  cJSON_Delete(cmd);

  cJSON *pub_obj = cJSON_CreateObject();
  cJSON_AddStringToObject(pub_obj, "topic", target->topic_sub);
  cJSON_AddStringToObject(pub_obj, "type", "pub");
  cJSON_AddStringToObject(pub_obj, "payload", cmd_str);
  free(cmd_str);

  char *pub_str = cJSON_PrintUnformatted(pub_obj);
  cJSON_Delete(pub_obj);

  cJSON *envelope = cJSON_CreateObject();
  cJSON_AddStringToObject(envelope, "data", pub_str);
  cJSON_AddNumberToObject(envelope, "ts", (double)now_ms);
  free(pub_str);

  char *plaintext = cJSON_PrintUnformatted(envelope);
  cJSON_Delete(envelope);

  char *enc_b64 = (char *)malloc(2048);
  if (!enc_b64) {
    free(plaintext);
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = hunonic_crypto_encrypt(plaintext, enc_b64, 2048);
  free(plaintext);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Ma hoa lenh dieu khien that bai!");
    free(enc_b64);
    return err;
  }

  char *ws_frame = (char *)malloc(2200);
  if (!ws_frame) {
    free(enc_b64);
    return ESP_ERR_NO_MEM;
  }
  snprintf(ws_frame, 2200, "{\"data\":\"%s\"}", enc_b64);
  free(enc_b64);

  ESP_LOGI(TAG,
           "-> BAN LENH QUA WS: Dieu hoa [%s], Power=%d, Temp=%d, Mode=%d, "
           "Fan=%d",
           target->id, power, temp, mode, fan);

  int sent = esp_websocket_client_send_text(
      s_ws_client, ws_frame, strlen(ws_frame), pdMS_TO_TICKS(2000));
  if (sent < 0) {
    ESP_LOGW(TAG, "Gui frame WS lan 1 that bai, thu lai sau 300ms...");
    vTaskDelay(pdMS_TO_TICKS(300));
    sent = esp_websocket_client_send_text(
        s_ws_client, ws_frame, strlen(ws_frame), pdMS_TO_TICKS(2000));
  }
  free(ws_frame);

  if (sent < 0) {
    ESP_LOGE(TAG, "Gui frame qua WebSocket that bai");
    return ESP_FAIL;
  }

  target->power = power;
  target->temp = temp;
  target->mode = mode;
  target->fan = fan;

  return ESP_OK;
}

esp_err_t hunonic_bridge_set_power(const char *device_id, bool power) {
  for (int i = 0; i < s_ac_count; i++) {
    if (s_ac_devices[i].is_valid &&
        strcmp(s_ac_devices[i].id, device_id) == 0) {
      return hunonic_bridge_control(
          device_id, power ? 1 : 0,
          s_ac_devices[i].temp > 0 ? s_ac_devices[i].temp : 25,
          s_ac_devices[i].mode, s_ac_devices[i].fan);
    }
  }
  return ESP_ERR_NOT_FOUND;
}

esp_err_t hunonic_bridge_set_temp(const char *device_id, int temp) {
  for (int i = 0; i < s_ac_count; i++) {
    if (s_ac_devices[i].is_valid &&
        strcmp(s_ac_devices[i].id, device_id) == 0) {
      return hunonic_bridge_control(device_id, s_ac_devices[i].power, temp,
                                    s_ac_devices[i].mode, s_ac_devices[i].fan);
    }
  }
  return ESP_ERR_NOT_FOUND;
}

const hunonic_ac_device_t *hunonic_bridge_get_devices(int *out_count) {
  if (out_count)
    *out_count = s_ac_count;
  return s_ac_devices;
}

bool hunonic_bridge_is_connected(void) { return s_is_ws_connected; }

void hunonic_bridge_register_state_cb(hunonic_ac_state_cb_t cb, void *arg) {
  s_state_cb = cb;
  s_state_cb_arg = arg;
}

esp_err_t hunonic_bridge_init(const char *phone, const char *password) {
  ESP_LOGI(TAG, "Khoi dong Hunonic Integration Bridge ");

  if (phone)
    strncpy(s_phone, phone, sizeof(s_phone) - 1);
  if (password)
    strncpy(s_password, password, sizeof(s_password) - 1);

  if (nvs_load_string("hun_token", s_token, sizeof(s_token)) == ESP_OK &&
      strlen(s_token) > 0) {
    ESP_LOGI(TAG, "Da tim thay Token trong NVS Flash: %.20s...", s_token);
    parse_user_id_from_jwt(s_token);
  } else {
    ESP_LOGI(TAG, "Chua co Token trong NVS. Tien hanh dang nhap HTTP...");
    esp_err_t err = hunonic_http_login(s_phone, s_password);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Khong the dang nhap vao Hunonic Cloud!");
      return err;
    }
  }

  esp_err_t err = hunonic_bridge_sync_devices();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Dong bo danh sach thiet bi that bai, se thu lai sau");
  }

  err = hunonic_ws_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Khoi dong WebSocket client that bai: %d", err);
    return err;
  }

  return ESP_OK;
}