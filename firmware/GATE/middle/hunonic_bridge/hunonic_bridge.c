#include "hunonic_bridge.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "flash.h"
#include "hunonic_crypto.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "mbedtls/md5.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "HUNONIC_BRIDGE";

#define HUNONIC_API_LOGIN_URL "https://web.hunonic.com/api/api/auth/login"
#define HUNONIC_API_HOME_LIST_URL                                               \
  "https://web.hunonic.com/api/api/hun-api/home/list-home"
#define HUNONIC_API_DEVICE_LIST_URL                                            \
  "https://web.hunonic.com/api/api/hun-api/device/listDeviceOfHomeSelect?home_id="
#define HUNONIC_WS_GATEWAY_URL "wss://web.hunonic.com/gateway/ws?token="

#define HTTP_RESP_BUFFER_SIZE 16384
#define TOKEN_MAX_LEN 512
#define PHONE_MAX_LEN 32
#define PASS_MAX_LEN 64
#define MAX_WS_SUBSCRIBED_TOPICS (MAX_HUNONIC_AC_DEVICES * 2 + 4)

static char s_token[TOKEN_MAX_LEN] = {0};
static char s_user_id[32] = {0};
static char s_phone[PHONE_MAX_LEN] = {0};
static char s_password[PASS_MAX_LEN] = {0};
static char s_ir_gateway_root_id[128] = {0};
static char s_ir_gateway_topic_sub[128] = {0};
static char s_ir_gateway_topic_pub[128] = {0};
static bool s_is_ir_hub_online = false;
static char s_selected_home_id[32];
static esp_err_t s_last_sync_error = ESP_ERR_INVALID_STATE;
static int s_last_sync_http_status = 0;

static void hunonic_ws_subscribe_all(void);
static esp_err_t hunonic_ws_start(void);

static hunonic_ac_device_t s_ac_devices[MAX_HUNONIC_AC_DEVICES];
static int s_ac_count = 0;
static hunonic_ac_device_t s_previous_ac_devices[MAX_HUNONIC_AC_DEVICES];
static int s_previous_ac_count = 0;

static esp_websocket_client_handle_t s_ws_client = NULL;
static bool s_is_ws_connected = false;
/* TLS sync needs sizeable mbedTLS buffers.  Never run it alongside command
 * encryption, otherwise esp-aes can fail to allocate memory. */
static SemaphoreHandle_t s_hunonic_io_mutex = NULL;
static char s_ws_subscribed_topics[MAX_WS_SUBSCRIBED_TOPICS][128];
static int s_ws_subscribed_topic_count = 0;

static hunonic_ac_state_cb_t s_state_cb = NULL;
static void *s_state_cb_arg = NULL;

static const char *json_string(const cJSON *item) {
  return (item && cJSON_IsString(item) && item->valuestring)
             ? item->valuestring
             : "";
}

static void json_id_to_string(const cJSON *item, char *out, size_t out_size) {
  if (!out || out_size == 0) return;
  out[0] = '\0';
  if (item && cJSON_IsString(item) && item->valuestring) {
    strlcpy(out, item->valuestring, out_size);
  } else if (item && cJSON_IsNumber(item)) {
    snprintf(out, out_size, "%d", item->valueint);
  } else if (item && cJSON_IsObject(item)) {
    json_id_to_string(cJSON_GetObjectItem(item, "value"), out, out_size);
  }
}

static int json_to_int(const cJSON *item) {
  if (!item) return 0;
  if (cJSON_IsNumber(item)) return item->valueint;
  return cJSON_IsString(item) && item->valuestring ? atoi(item->valuestring)
                                                     : 0;
}

static bool is_ir_hub_type(const char *type) {
  return type && (strcasecmp(type, "irwifi") == 0 ||
                  strcasecmp(type, "irwifiv2") == 0);
}

static bool is_ac_category(const char *category) {
  return category && (strcasecmp(category, "AC") == 0 ||
                      strcasecmp(category, "air conditioner") == 0 ||
                      strcasecmp(category, "airconditioner") == 0);
}

/* The official IR detail view groups children by their hub's topicsub.
 * A stale irchild may remain in the home response after its hub is removed. */
static cJSON *find_current_ir_parent(cJSON *rooms, cJSON *child) {
  const char *child_topic = json_string(cJSON_GetObjectItem(child, "topicsub"));
  char parent_id[128];
  json_id_to_string(cJSON_GetObjectItem(child, "irchild_root_id"), parent_id, sizeof(parent_id));
  cJSON *room = NULL;
  cJSON_ArrayForEach(room, rooms) {
    cJSON *device = NULL;
    cJSON *devices = cJSON_GetObjectItem(room, "devices");
    cJSON_ArrayForEach(device, devices) {
      if (!is_ir_hub_type(json_string(cJSON_GetObjectItem(device, "type")))) continue;
      const char *hub_topic = json_string(cJSON_GetObjectItem(device, "topicsub"));
      if (child_topic[0] && hub_topic[0]) {
        if (strcmp(child_topic, hub_topic) == 0) return device;
        continue;
      }
      char hub_id[128];
      json_id_to_string(cJSON_GetObjectItem(device, "id"), hub_id, sizeof(hub_id));
      if (parent_id[0] && strcmp(parent_id, hub_id) == 0) return device;
    }
  }
  return NULL;
}

static bool is_numeric_id(const char *value) {
  if (!value || !value[0]) return false;
  for (const char *p = value; *p; p++) {
    if (*p < '0' || *p > '9') return false;
  }
  return true;
}

static const hunonic_ac_device_t *find_previous_ac(const char *id) {
  for (int i = 0; id && i < s_previous_ac_count; i++) {
    if (s_previous_ac_devices[i].is_valid &&
        strcmp(s_previous_ac_devices[i].id, id) == 0) {
      return &s_previous_ac_devices[i];
    }
  }
  return NULL;
}

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
      if (id_item && cJSON_IsString(id_item) && id_item->valuestring) {
        strncpy(s_user_id, id_item->valuestring, sizeof(s_user_id) - 1);
        ESP_LOGI(TAG, "Da trich xuat user_id tu JWT: %s", s_user_id);
      } else if (id_item && cJSON_IsNumber(id_item)) {
        snprintf(s_user_id, sizeof(s_user_id), "%d", id_item->valueint);
        ESP_LOGI(TAG, "Da trich xuat user_id tu JWT: %s", s_user_id);
      }
      cJSON_Delete(jwt_json);
    }
  }
}

typedef struct {
  char *buffer;
  int buffer_len;
  bool overflow;
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
        resp->overflow = true;
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

/* Fetch a protected API resource and return its complete JSON body. */
static esp_err_t hunonic_http_get(const char *url, char **out_response,
                                  int *out_status) {
  if (!url || !out_response || !out_status) return ESP_ERR_INVALID_ARG;
  *out_response = NULL;
  *out_status = 0;

  char *response = malloc(HTTP_RESP_BUFFER_SIZE);
  if (!response) return ESP_ERR_NO_MEM;
  memset(response, 0, HTTP_RESP_BUFFER_SIZE);
  http_response_buffer_t buffer = {.buffer = response, .buffer_len = 0};
  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_GET,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .event_handler = http_event_handler,
      .user_data = &buffer,
      .timeout_ms = 15000,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    free(response);
    return ESP_ERR_NO_MEM;
  }
  char auth_header[TOKEN_MAX_LEN + 16];
  snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_token);
  esp_http_client_set_header(client, "Authorization", auth_header);
  esp_http_client_set_header(client, "Accept", "application/json");
  esp_http_client_set_header(client, "Cache-Control", "no-cache, no-store");
  esp_http_client_set_header(client, "Pragma", "no-cache");

  esp_err_t err = esp_http_client_perform(client);
  *out_status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  *out_response = response;
  return buffer.overflow ? ESP_ERR_INVALID_SIZE : err;
}

/* The current API requires a home_id before it returns rooms and devices. */
static esp_err_t hunonic_get_first_home_id(char *out_home_id, size_t out_size,
                                           int *out_status) {
  char *response = NULL;
  esp_err_t err = hunonic_http_get(HUNONIC_API_HOME_LIST_URL, &response,
                                   out_status);
  if (err != ESP_OK || *out_status != 200) {
    free(response);
    return ESP_FAIL;
  }
  cJSON *root = cJSON_Parse(response);
  free(response);
  if (!root) return ESP_FAIL;
  cJSON *homes = cJSON_GetObjectItem(root, "data");
  cJSON *home = homes && cJSON_IsArray(homes) ? cJSON_GetArrayItem(homes, 0) : NULL;
  if (s_selected_home_id[0] && homes && cJSON_IsArray(homes)) {
    cJSON *candidate = NULL;
    home = NULL;
    cJSON_ArrayForEach(candidate, homes) {
      char candidate_id[32];
      json_id_to_string(cJSON_GetObjectItem(candidate, "id"), candidate_id, sizeof(candidate_id));
      if (strcmp(candidate_id, s_selected_home_id) == 0) { home = candidate; break; }
    }
  }
  cJSON *id = home ? cJSON_GetObjectItem(home, "id") : NULL;
  if (cJSON_IsString(id) && id->valuestring) {
    strlcpy(out_home_id, id->valuestring, out_size);
  } else if (cJSON_IsNumber(id)) {
    snprintf(out_home_id, out_size, "%d", id->valueint);
  }
  cJSON_Delete(root);
  if (out_home_id[0]) strlcpy(s_selected_home_id, out_home_id, sizeof(s_selected_home_id));
  return out_home_id[0] ? ESP_OK : ESP_ERR_NOT_FOUND;
}

/* Match the official web client's selected-home snapshot validation. */
static cJSON *select_valid_home_snapshot(cJSON *root, const char *home_id) {
  cJSON *api_status = cJSON_GetObjectItem(root, "status");
  cJSON *homes_data = cJSON_GetObjectItem(root, "data");
  cJSON *selected_home = NULL;
  cJSON *candidate = NULL;
  if (cJSON_IsTrue(api_status) && cJSON_IsArray(homes_data)) {
    cJSON_ArrayForEach(candidate, homes_data) {
      char candidate_id[32];
      json_id_to_string(cJSON_GetObjectItem(candidate, "id"), candidate_id, sizeof(candidate_id));
      if (strcmp(candidate_id, home_id) == 0) { selected_home = candidate; break; }
    }
  }
  cJSON *selected_rooms = selected_home ? cJSON_GetObjectItem(selected_home, "rooms") : NULL;
  bool snapshot_valid = cJSON_IsArray(selected_rooms);
  cJSON *validated_room = NULL;
  if (snapshot_valid) {
    cJSON_ArrayForEach(validated_room, selected_rooms) {
      if (!cJSON_IsArray(cJSON_GetObjectItem(validated_room, "devices"))) {
        snapshot_valid = false;
        break;
      }
    }
  }
  return snapshot_valid ? selected_home : NULL;
}

esp_err_t hunonic_bridge_sync_devices(void) {
  if (strlen(s_token) == 0) {
    ESP_LOGW(TAG, "Chua co token, khong the sync thiet bi");
    s_last_sync_error = ESP_ERR_INVALID_STATE;
    s_last_sync_http_status = 0;
    return ESP_ERR_INVALID_STATE;
  }

  if (!s_hunonic_io_mutex) {
    s_hunonic_io_mutex = xSemaphoreCreateMutex();
    if (!s_hunonic_io_mutex) {
      s_last_sync_error = ESP_ERR_NO_MEM;
      s_last_sync_http_status = 0;
      return ESP_ERR_NO_MEM;
    }
  }
  xSemaphoreTake(s_hunonic_io_mutex, portMAX_DELAY);

  char *resp_buf = NULL;
  for (int attempt = 0; attempt < 2; attempt++) {
    char home_id[32] = {0};
    int status_code = 0;
    ESP_LOGI(TAG, "Dang lay danh sach nha Hunonic (lan %d)...", attempt + 1);
    esp_err_t err = hunonic_get_first_home_id(home_id, sizeof(home_id),
                                               &status_code);
    if (err == ESP_OK) {
      char device_url[192];
      /* Same force-refresh parameter used by the Hunonic web client. */
      snprintf(device_url, sizeof(device_url), "%s%s&_ts=%lld",
               HUNONIC_API_DEVICE_LIST_URL, home_id,
               (long long)esp_timer_get_time());
      ESP_LOGI(TAG, "Dang dong bo thiet bi Hunonic cua home %s...", home_id);
      err = hunonic_http_get(device_url, &resp_buf, &status_code);
    }

    if (err != ESP_OK || status_code != 200) {
      s_last_sync_error = (err != ESP_OK) ? err : ESP_FAIL;
      s_last_sync_http_status = status_code;
      ESP_LOGE(TAG, "Sync thiet bi that bai: err=%d, HTTP status=%d", err,
               status_code);
      if (resp_buf && strlen(resp_buf) > 0) {
        ESP_LOGE(TAG, "Phan hoi tu server: %s", resp_buf);
      }
      free(resp_buf);
      resp_buf = NULL;

      /* Hunonic currently reports a replaced/invalidated session as either
       * 401/403 or HTTP 500.  Re-login once so the Gateway can recover after
       * another client signs in with the same account. */
      if (attempt == 0 &&
          (status_code == 401 || status_code == 403 || status_code == 500) &&
          strlen(s_phone) > 0 && strlen(s_password) > 0) {
        ESP_LOGW(
            TAG,
            "Phat hien Token bi mat quyen/thiet bi khac dang nhap (HTTP %d)!",
            status_code);
        ESP_LOGW(TAG, "Gate dang tu dong dang nhap lai de lay Token chu quyen "
                      "& da thiet bi khac ra...");

        if (hunonic_http_login(s_phone, s_password) == ESP_OK) {
          ESP_LOGI(TAG, "Re-login thanh cong, thu dong bo lai voi token moi...");
          continue;
        }
      }
      xSemaphoreGive(s_hunonic_io_mutex);
      return ESP_FAIL;
    }

    break;
  }

  if (!resp_buf) {
    s_last_sync_error = ESP_FAIL;
    s_last_sync_http_status = 0;
    xSemaphoreGive(s_hunonic_io_mutex);
    return ESP_FAIL;
  }

  cJSON *root = cJSON_Parse(resp_buf);
  free(resp_buf);
  if (!root) {
    ESP_LOGE(TAG, "Parse JSON listDeviceByHome that bai");
    s_last_sync_error = ESP_FAIL;
    s_last_sync_http_status = 200;
    xSemaphoreGive(s_hunonic_io_mutex);
    return ESP_FAIL;
  }

  /* Validate the entire selected home before replacing the last good list.
   * HTTP 200 alone can contain an API error or a different home's snapshot. */
  cJSON *selected_home = select_valid_home_snapshot(root, s_selected_home_id);
  if (!selected_home) {
    ESP_LOGE(TAG, "Invalid fresh snapshot for selected home %s; preserving last good inventory", s_selected_home_id);
    cJSON_Delete(root);
    s_last_sync_error = ESP_FAIL;
    s_last_sync_http_status = 200;
    xSemaphoreGive(s_hunonic_io_mutex);
    return ESP_FAIL;
  }

  cJSON *selected_rooms = cJSON_GetObjectItem(selected_home, "rooms");
  memcpy(s_previous_ac_devices, s_ac_devices, sizeof(s_ac_devices));
  s_previous_ac_count = s_ac_count;
  s_ac_count = 0;
  memset(s_ac_devices, 0, sizeof(s_ac_devices));

  cJSON *data_arr = cJSON_GetObjectItem(root, "data");
  if (data_arr && cJSON_IsArray(data_arr)) {
    bool found_ir_in_sync = false;
    bool ir_online_in_sync = false;

    cJSON *home = NULL;
    cJSON_ArrayForEach(home, data_arr) {
      if (home != selected_home) continue;
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
              cJSON *irchild_root_id =
                  cJSON_GetObjectItem(dev, "irchild_root_id");
              cJSON *dev_id_obj = cJSON_GetObjectItem(dev, "id");
              char dev_id_str[sizeof(s_ir_gateway_root_id)] = {0};
              char root_id_str[sizeof(s_ir_gateway_root_id)] = {0};
              char irchild_root_id_str[sizeof(s_ir_gateway_root_id)] = {0};
              json_id_to_string(dev_id_obj, dev_id_str, sizeof(dev_id_str));
              json_id_to_string(root_id, root_id_str, sizeof(root_id_str));
              json_id_to_string(irchild_root_id, irchild_root_id_str,
                                sizeof(irchild_root_id_str));
              const char *type_str = json_string(type);
              const char *root_type_str = json_string(root_type);

              /* Hunonic's root_id field on an IR hub may be an encrypted
               * transport key, not its numeric device ID.  AC_CONTROL needs
               * the hub device id (for example 3987670), so prefer dev.id. */
              bool is_ir_hub = is_ir_hub_type(type_str);
              if (is_ir_hub) {
                if (is_numeric_id(dev_id_str)) {
                  strncpy(s_ir_gateway_root_id, dev_id_str,
                          sizeof(s_ir_gateway_root_id) - 1);
                } else if (is_numeric_id(root_id_str)) {
                  strncpy(s_ir_gateway_root_id, root_id_str,
                          sizeof(s_ir_gateway_root_id) - 1);
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

                cJSON *st = cJSON_GetObjectItem(dev, "state");
                bool is_st_online = false;
                if (st) {
                  if (cJSON_IsString(st) && st->valuestring) {
                    is_st_online = (strcmp(st->valuestring, "1") == 0);
                  } else if (cJSON_IsNumber(st)) {
                    is_st_online = (st->valueint == 1);
                  }
                }
                found_ir_in_sync = true;
                ir_online_in_sync = is_st_online;
                ESP_LOGI(TAG, "Hunonic API: Tram IR Hub [%s]: %s (state raw: %s)",
                         s_ir_gateway_root_id,
                         is_st_online ? "ONLINE (Dang hoat dong)" : "OFFLINE (Rut nguon / Chua cam dien)",
                         st ? (cJSON_IsString(st) ? st->valuestring : "num") : "null");
              }

              cJSON *cat = cJSON_GetObjectItem(dev, "category");
              cJSON *cat_name =
                  cat ? cJSON_GetObjectItem(cat, "name_en") : NULL;
              /* Hunonic has changed category strings across API versions.
               * An irchild beneath an IR hub is still an AC even if category
               * metadata is absent, so detect both representations. */
              bool is_ac = !is_ir_hub &&
                           (strcasecmp(type_str, "irchild") == 0 ||
                            is_ac_category(json_string(cat_name)) ||
                            (root_id_str[0] && is_ir_hub_type(root_type_str)));

              cJSON *parent_hub = is_ac ? find_current_ir_parent(selected_rooms, dev) : NULL;
              if (is_ac && !parent_hub) {
                ESP_LOGW(TAG, "[SYNC SKIP ORPHAN] AC id=%s has no matching IR hub in home %s",
                         dev_id_str, s_selected_home_id);
                continue;
              }
              if (is_ac) {
                char parent_id[128];
                json_id_to_string(cJSON_GetObjectItem(parent_hub, "id"), parent_id, sizeof(parent_id));
                ESP_LOGI(TAG, "[SYNC AC PARENT] AC id=%s hub=%s", dev_id_str, parent_id);
              }

              bool duplicate = false;
              for (int i = 0; i < s_ac_count; i++) {
                if (dev_id_str[0] && strcmp(s_ac_devices[i].id, dev_id_str) == 0) {
                  duplicate = true;
                  break;
                }
              }

              if (is_ac && !duplicate && s_ac_count < MAX_HUNONIC_AC_DEVICES) {
                hunonic_ac_device_t *ac = &s_ac_devices[s_ac_count];
                cJSON *id = cJSON_GetObjectItem(dev, "id");
                cJSON *name = cJSON_GetObjectItem(dev, "name");
                cJSON *t_sub = cJSON_GetObjectItem(dev, "topicsub");
                cJSON *t_pub = cJSON_GetObjectItem(dev, "topicpub");
                cJSON *val = cJSON_GetObjectItem(dev, "value");
                cJSON *brand_obj = cJSON_GetObjectItem(dev, "brand");
                cJSON *brand_id =
                    brand_obj ? cJSON_GetObjectItem(brand_obj, "id") : NULL;

                json_id_to_string(id, ac->id, sizeof(ac->id));
                /* Some API revisions provide a numeric irchild_root_id;
                 * others provide root_id as an opaque routing token. */
                if (is_numeric_id(irchild_root_id_str)) {
                  strlcpy(ac->root_id, irchild_root_id_str,
                          sizeof(ac->root_id));
                  ac->root_id_is_encrypted = false;
                } else if (strchr(root_id_str, ':') != NULL) {
                  strlcpy(ac->root_id, root_id_str, sizeof(ac->root_id));
                  ac->root_id_is_encrypted = true;
                } else {
                  strlcpy(ac->root_id, s_ir_gateway_root_id,
                          sizeof(ac->root_id));
                  ac->root_id_is_encrypted = false;
                }
                if (name && name->valuestring)
                  strncpy(ac->name, name->valuestring, sizeof(ac->name) - 1);
                if (t_sub && t_sub->valuestring)
                  strncpy(ac->topic_sub, t_sub->valuestring,
                          sizeof(ac->topic_sub) - 1);
                if (t_pub && t_pub->valuestring)
                  strncpy(ac->topic_pub, t_pub->valuestring,
                          sizeof(ac->topic_pub) - 1);
                ac->brand = json_to_int(brand_id);
                if (ac->brand == 0)
                  ac->brand = json_to_int(brand_obj);

                if (val && val->valuestring) {
                  cJSON *v_json = cJSON_Parse(val->valuestring);
                  if (v_json) {
                    cJSON *p = cJSON_GetObjectItem(v_json, "power");
                    cJSON *t = cJSON_GetObjectItem(v_json, "temp");
                    cJSON *m = cJSON_GetObjectItem(v_json, "mode");
                    cJSON *f = cJSON_GetObjectItem(v_json, "fan");
                    cJSON *b = cJSON_GetObjectItem(v_json, "brand");
                    if (p) {
                      ac->power = p->valueint;
                    }
                    if (t) {
                      ac->temp = t->valueint;
                    }
                    if (m) {
                      ac->mode = m->valueint;
                    }
                    if (f) {
                      ac->fan = f->valueint;
                    }
                    if (b && ac->brand == 0)
                      ac->brand = json_to_int(b);
                    cJSON_Delete(v_json);
                  }
                }

                ac->is_valid = true;
                ESP_LOGI(TAG,
                         "-> Phat hien Dieu hoa: [%s] '%s' (Brand %d) | "
                         "Power=%d, Temp=%d, Mode=%d, Fan=%d",
                         ac->id, ac->name, ac->brand, ac->power, ac->temp,
                         ac->mode, ac->fan);

                s_ac_count++;
              }
            }
          }
        }
      }
    }

    if (found_ir_in_sync) {
      s_is_ir_hub_online = ir_online_in_sync;
    } else {
      s_is_ir_hub_online = false;
      ESP_LOGW(TAG, "Khong tim thay Tram IR Hub trong tai khoan Hunonic -> OFFLINE");
    }
  }

  cJSON_Delete(root);
  for (int i = 0; i < s_previous_ac_count; ++i) {
    bool found = false;
    for (int j = 0; j < s_ac_count; ++j) {
      if (strcmp(s_previous_ac_devices[i].id, s_ac_devices[j].id) == 0) { found = true; break; }
    }
    if (!found) ESP_LOGI(TAG, "[SYNC REMOVED] AC id=%s", s_previous_ac_devices[i].id);
  }
  for (int i = 0; i < s_ac_count; ++i) {
    const hunonic_ac_device_t *old = find_previous_ac(s_ac_devices[i].id);
    const hunonic_ac_device_t *ac = &s_ac_devices[i];
    if (s_state_cb && (!old || old->power != ac->power || old->temp != ac->temp ||
                      old->mode != ac->mode || old->fan != ac->fan || old->brand != ac->brand)) {
      s_state_cb(ac, s_state_cb_arg);
    }
  }
  ESP_LOGI(TAG, "Dong bo hoan tat: tim thay %d dieu hoa", s_ac_count);

  if (s_is_ws_connected) {
    hunonic_ws_subscribe_all();
  } else if (!s_ws_client) {
    /* A later periodic sync can recover a failed first login without rebooting. */
    esp_err_t ws_err = hunonic_ws_start();
    if (ws_err != ESP_OK) {
      ESP_LOGE(TAG, "Khoi dong WebSocket sau khi sync that bai: %d", ws_err);
      s_last_sync_error = ws_err;
      s_last_sync_http_status = 200;
      xSemaphoreGive(s_hunonic_io_mutex);
      return ws_err;
    }
  }
  xSemaphoreGive(s_hunonic_io_mutex);
  s_last_sync_error = ESP_OK;
  s_last_sync_http_status = 200;
  return ESP_OK;
}

static char *s_ws_rx_buf = NULL;
static int s_ws_rx_len = 0;

static esp_err_t hunonic_ws_subscribe(const char *topic) {
  if (!s_is_ws_connected || !s_ws_client || !topic || strlen(topic) == 0) {
    return ESP_ERR_INVALID_STATE;
  }

  for (int i = 0; i < s_ws_subscribed_topic_count; i++) {
    if (strcmp(s_ws_subscribed_topics[i], topic) == 0) {
      return ESP_OK;
    }
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

  ESP_LOGI(TAG, "-> [WS SUBSCRIBE] Topic: %s", topic);
  int sent = esp_websocket_client_send_text(
      s_ws_client, ws_frame, strlen(ws_frame), pdMS_TO_TICKS(2000));
  free(ws_frame);

  if (sent >= 0) {
    if (s_ws_subscribed_topic_count < MAX_WS_SUBSCRIBED_TOPICS) {
      strlcpy(s_ws_subscribed_topics[s_ws_subscribed_topic_count++], topic,
              sizeof(s_ws_subscribed_topics[0]));
    }
    return ESP_OK;
  }
  return ESP_FAIL;
}

static void hunonic_ws_subscribe_all(void) {
  if (!s_is_ws_connected || !s_ws_client) {
    return;
  }
  ESP_LOGI(TAG, "=== Bat dau Subscribe cac topic Hunonic Cloud ===");

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

  if (device_id && strlen(device_id) > 0) {
    for (int i = 0; i < s_ac_count; i++) {
      if (s_ac_devices[i].is_valid &&
          strcmp(s_ac_devices[i].id, device_id) == 0) {
        target = &s_ac_devices[i];
        break;
      }
    }
  }

  if (!target && (!device_id || !device_id[0]) && brand > 0) {
    for (int i = 0; i < s_ac_count; i++) {
      if (s_ac_devices[i].is_valid && s_ac_devices[i].brand == brand) {
        target = &s_ac_devices[i];
        break;
      }
    }
  }

  if (!target && (!device_id || !device_id[0]) && s_ac_count == 1 && s_ac_devices[0].is_valid) {
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
           "[2-WAY SYNC OK] Dieu hoa [%s] '%s' -> Nguon: %s | "
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
    ESP_LOGI(TAG, "WebSocket Hunonic DA KET NOI THANH CONG!");
    s_is_ws_connected = true;
    s_ws_subscribed_topic_count = 0;
    memset(s_ws_subscribed_topics, 0, sizeof(s_ws_subscribed_topics));
    hunonic_ws_subscribe_all();
    break;

  case WEBSOCKET_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "WebSocket Hunonic bi mat ket noi!");
    s_is_ws_connected = false;
    s_ws_subscribed_topic_count = 0;
    if (s_ws_rx_buf) {
      free(s_ws_rx_buf);
      s_ws_rx_buf = NULL;
      s_ws_rx_len = 0;
    }
    break;

  case WEBSOCKET_EVENT_DATA:
    if (data->data_len > 0) {

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
  if (device_id && strlen(device_id) > 0) {
    for (int i = 0; i < s_ac_count; i++) {
      if (s_ac_devices[i].is_valid &&
          strcmp(s_ac_devices[i].id, device_id) == 0) {
        target = &s_ac_devices[i];
        break;
      }
    }
  }

  /* A device may have been added in the Hunonic app since the last periodic
   * sync.  Refresh once and resolve its exact ID before sending a command. */
  if (!target && device_id && strlen(device_id) > 0) {
    ESP_LOGI(TAG, "ID dieu hoa [%s] chua co trong cache, dong bo ngay...",
             device_id);
    esp_err_t sync_err = hunonic_bridge_sync_devices();
    if (sync_err != ESP_OK) {
      ESP_LOGE(TAG, "Khong the dong bo de tim dieu hoa [%s]: %d", device_id,
               sync_err);
      return sync_err;
    }
    for (int i = 0; i < s_ac_count; i++) {
      if (s_ac_devices[i].is_valid &&
          strcmp(s_ac_devices[i].id, device_id) == 0) {
        target = &s_ac_devices[i];
        break;
      }
    }
  }

  /* Compatibility path for callers that intentionally omit an ID.  Never
   * fall back to the first AC when a concrete, unknown ID was requested. */
  if (!target && (!device_id || strlen(device_id) == 0) &&
      s_ac_count > 0 && s_ac_devices[0].is_valid) {
    target = &s_ac_devices[0];
  }

  if (!target) {
    ESP_LOGE(TAG, "Khong tim thay dieu hoa voi ID: %s", device_id ? device_id : "(null)");
    return ESP_ERR_NOT_FOUND;
  }

  if (!s_hunonic_io_mutex) {
    s_hunonic_io_mutex = xSemaphoreCreateMutex();
    if (!s_hunonic_io_mutex) return ESP_ERR_NO_MEM;
  }
  /* If a periodic TLS sync is in progress, wait rather than dropping the
   * command because mbedTLS/AES compete for the same heap. */
  xSemaphoreTake(s_hunonic_io_mutex, portMAX_DELAY);

  int64_t now_ms = (int64_t)(esp_timer_get_time() / 1000);
  /* Captured from the official Hunonic web client: AC_CONTROL always wraps
   * the numeric IR hub device ID in a fresh encrypted root_id token. */
  const char *r_id = s_ir_gateway_root_id;
  char encrypted_root_id[256] = {0};
  if (!is_numeric_id(r_id) ||
      hunonic_crypto_encrypt(r_id, encrypted_root_id,
                             sizeof(encrypted_root_id)) != ESP_OK) {
    ESP_LOGE(TAG, "Khong ma hoa duoc IR Hub ID cho dieu hoa [%s]", target->id);
    xSemaphoreGive(s_hunonic_io_mutex);
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "Gui AC_CONTROL: AC=%s, IR Hub=%s, brand=%d", target->id,
           r_id, target->brand);

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
  cJSON_AddStringToObject(cmd, "root_id", encrypted_root_id);
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
    xSemaphoreGive(s_hunonic_io_mutex);
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = hunonic_crypto_encrypt(plaintext, enc_b64, 2048);
  free(plaintext);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Ma hoa lenh dieu khien that bai!");
    free(enc_b64);
    xSemaphoreGive(s_hunonic_io_mutex);
    return err;
  }

  char *ws_frame = (char *)malloc(2200);
  if (!ws_frame) {
    free(enc_b64);
    xSemaphoreGive(s_hunonic_io_mutex);
    return ESP_ERR_NO_MEM;
  }
  snprintf(ws_frame, 2200, "{\"data\":\"%s\"}", enc_b64);
  free(enc_b64);

  ESP_LOGI(TAG,
           "-> BAN LENH QUA WS: Dieu hoa [%s], Power=%d, Temp=%d, Mode=%d, "
           "Fan=%d, User=%d",
           target->id, power, temp, mode, fan, atoi(s_user_id));

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
    xSemaphoreGive(s_hunonic_io_mutex);
    return ESP_FAIL;
  }

  target->power = power;
  target->temp = temp;
  target->mode = mode;
  target->fan = fan;

  xSemaphoreGive(s_hunonic_io_mutex);
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

esp_err_t hunonic_bridge_get_last_sync_error(void) { return s_last_sync_error; }

int hunonic_bridge_get_last_sync_http_status(void) {
  return s_last_sync_http_status;
}

bool hunonic_bridge_is_connected(void) { return s_is_ws_connected; }

bool hunonic_bridge_is_ir_hub_online(void) {
  return s_is_ir_hub_online;
}

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
    ESP_LOGE(TAG, "Dong bo danh sach thiet bi that bai; khong mo WebSocket voi token chua duoc xac thuc");
    return err;
  }

  if (!s_ws_client) {
    err = hunonic_ws_start();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Khoi dong WebSocket client that bai: %d", err);
      return err;
    }
  }

  return ESP_OK;
}

esp_err_t hunonic_bridge_stop(void) {
  s_is_ws_connected = false;
  if (s_ws_client) {
    ESP_LOGI(TAG, "Tam dung WebSocket Hunonic Bridge (cho ket noi lai Wi-Fi)...");
    esp_websocket_client_stop(s_ws_client);
  }
  return ESP_OK;
}
