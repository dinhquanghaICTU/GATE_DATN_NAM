#include "mqtt.h"
#include "config.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include <string.h>

static const char *TAG = "MIDDLE_MQTT";

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static EventGroupHandle_t s_mqtt_event_group = NULL;
static app_mqtt_state_t s_mqtt_state = MQTT_STATE_DISCONNECTED;
static app_mqtt_data_cb_t s_data_callback = NULL;
static void *s_data_arg = NULL;
static app_mqtt_state_cb_t s_state_callback = NULL;
static void *s_state_arg = NULL;

static void set_mqtt_state(app_mqtt_state_t state) {
  s_mqtt_state = state;
  if (s_state_callback) {
    s_state_callback(state, s_state_arg);
  }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT da ket noi thanh cong toi Broker!");
    set_mqtt_state(MQTT_STATE_CONNECTED);
    xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);

#ifdef TOPIC_GATE_COMMAND
    esp_mqtt_client_subscribe(s_mqtt_client, TOPIC_GATE_COMMAND, 1);
#endif
#ifdef TOPIC_GATE_OTA
    esp_mqtt_client_subscribe(s_mqtt_client, TOPIC_GATE_OTA, 1);
#endif
    break;

  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "MQTT mat ket noi toi Broker!");
    set_mqtt_state(MQTT_STATE_DISCONNECTED);
    xEventGroupClearBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
    break;

  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGI(TAG, "Subscribed thanh cong, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_DATA:
    ESP_LOGI(TAG, "Nhan tin nhan MQTT [Topic: %.*s]: %.*s",
             event->topic_len, event->topic, event->data_len, event->data);
    if (s_data_callback) {
      s_data_callback(event->topic, event->topic_len, event->data,
                      event->data_len, s_data_arg);
    }
    break;

  case MQTT_EVENT_ERROR:
    ESP_LOGE(TAG, "MQTT Event Error!");
    set_mqtt_state(MQTT_STATE_ERROR);
    break;

  default:
    break;
  }
}

typedef struct {
  char topic[64];
  char payload[512];
  int qos;
  int retain;
} mqtt_queue_item_t;

static QueueHandle_t s_mqtt_tx_queue = NULL;

static void mqtt_tx_task(void *pvParam) {
  ESP_LOGI(TAG, "MQTT TX Queue Worker da khoi dong!");
  mqtt_queue_item_t item;
  while (1) {
    if (xQueueReceive(s_mqtt_tx_queue, &item, portMAX_DELAY) == pdTRUE) {
      if (s_mqtt_client && s_mqtt_state == MQTT_STATE_CONNECTED) {
        esp_mqtt_client_publish(s_mqtt_client, item.topic, item.payload, 0, item.qos, item.retain);
        // Pacing delay (40ms) giua cac ban tin tranh nghen TCP/TLS socket va giu broker ping on dinh
        vTaskDelay(pdMS_TO_TICKS(40));
      }
    }
  }
}

esp_err_t app_mqtt_init(const app_mqtt_config_t *config) {
  if (s_mqtt_event_group == NULL) {
    s_mqtt_event_group = xEventGroupCreate();
  }

  if (s_mqtt_tx_queue == NULL) {
    s_mqtt_tx_queue = xQueueCreate(24, sizeof(mqtt_queue_item_t));
    xTaskCreate(mqtt_tx_task, "mqtt_tx_task", 4096, NULL, 3, NULL);
  }

  esp_mqtt_client_config_t mqtt_cfg = {
      .broker = {
          .address.uri = config ? config->uri : MQTT_BROKER_URI,
          .verification.crt_bundle_attach = esp_crt_bundle_attach,
      },
      .credentials = {
          .username = config ? config->username : MQTT_USERNAME,
          .authentication.password = config ? config->password : MQTT_PASSWORD,
          .client_id = (config && config->client_id) ? config->client_id : NULL,
      },
      .network = {
          .reconnect_timeout_ms = 5000,
          .timeout_ms = 10000,
      },
      .session = {
          .last_will = {
              .topic = TOPIC_GATE_STATUS,
              .msg = "{\"event\":\"lwt_offline\",\"device\":\"esp32s3_gate\",\"online\":false}",
              .qos = 1,
              .retain = 0,
          },
          .keepalive = 60,
      },
  };

  s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
  if (s_mqtt_client == NULL) {
    ESP_LOGE(TAG, "Khoi tao esp_mqtt_client that bai!");
    return ESP_FAIL;
  }

  esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                 mqtt_event_handler, NULL);
  ESP_LOGI(TAG, "Khoi tao MQTT Client thanh cong!");
  return ESP_OK;
}

esp_err_t app_mqtt_start(void) {
  if (!s_mqtt_client) return ESP_ERR_INVALID_STATE;
  set_mqtt_state(MQTT_STATE_CONNECTING);
  return esp_mqtt_client_start(s_mqtt_client);
}

esp_err_t app_mqtt_stop(void) {
  if (!s_mqtt_client) return ESP_ERR_INVALID_STATE;
  return esp_mqtt_client_stop(s_mqtt_client);
}

int app_mqtt_publish(const char *topic, const char *data, int qos, int retain) {
  if (!s_mqtt_tx_queue || !topic || !data) {
    return -1;
  }
  mqtt_queue_item_t item;
  strncpy(item.topic, topic, sizeof(item.topic) - 1);
  item.topic[sizeof(item.topic) - 1] = '\0';

  strncpy(item.payload, data, sizeof(item.payload) - 1);
  item.payload[sizeof(item.payload) - 1] = '\0';

  item.qos = qos;
  item.retain = retain;

  if (xQueueSend(s_mqtt_tx_queue, &item, pdMS_TO_TICKS(10)) != pdTRUE) {
    ESP_LOGW(TAG, "MQTT TX Queue day, bo qua ban tin");
    return -1;
  }
  return 0;
}

int app_mqtt_subscribe(const char *topic, int qos) {
  if (!s_mqtt_client) return -1;
  return esp_mqtt_client_subscribe(s_mqtt_client, topic, qos);
}

void app_mqtt_register_data_cb(app_mqtt_data_cb_t cb, void *arg) {
  s_data_callback = cb;
  s_data_arg = arg;
}

void app_mqtt_register_state_cb(app_mqtt_state_cb_t cb, void *arg) {
  s_state_callback = cb;
  s_state_arg = arg;
}

bool app_mqtt_is_connected(void) {
  return (s_mqtt_state == MQTT_STATE_CONNECTED);
}

bool app_mqtt_wait_for_connected(TickType_t timeout_ticks) {
  if (!s_mqtt_event_group) return false;
  EventBits_t bits = xEventGroupWaitBits(s_mqtt_event_group, MQTT_CONNECTED_BIT,
                                         pdFALSE, pdTRUE, timeout_ticks);
  return (bits & MQTT_CONNECTED_BIT) != 0;
}
