#include "ble.h"
#include "esp_log.h"
#include "flash.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "MIDDLE_BLE";

static char s_device_name[32] = "PRESENCE_C3_NODE";
static char s_wifi_ssid[BLE_MAX_SSID_LEN] = {0};
static char s_wifi_pass[BLE_MAX_PASS_LEN] = {0};
static char s_sensor_json[BLE_MAX_DATA_LEN] =
    "{\"temp\":0.0,\"hum\":0.0,\"presence\":0}";
static uint8_t s_node_id = 1;

static ble_wifi_config_cb_t s_config_cb = NULL;
static uint8_t s_own_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_sensor_val_handle = 0;
static bool s_ble_is_running = false;
static bool s_ble_stopping = false;
static bool s_is_connected = false;

static int gatt_svr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg);
static void ble_app_advertise(void);
static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_NODE_UUID),
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {

                    .uuid = BLE_UUID16_DECLARE(BLE_CHR_SSID_UUID),
                    .access_cb = gatt_svr_access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                },
                {

                    .uuid = BLE_UUID16_DECLARE(BLE_CHR_PASS_UUID),
                    .access_cb = gatt_svr_access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                },
                {

                    .uuid = BLE_UUID16_DECLARE(BLE_CHR_SENSOR_DATA_UUID),
                    .access_cb = gatt_svr_access_cb,
                    .val_handle = &s_sensor_val_handle,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                },
                {

                    .uuid = BLE_UUID16_DECLARE(BLE_CHR_NODE_ID_UUID),
                    .access_cb = gatt_svr_access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                },
                {
                    0,
                }},
    },
    {
        0,
    },
};

static int gatt_svr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
  uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);

  switch (ctxt->op) {
  case BLE_GATT_ACCESS_OP_READ_CHR:
    if (uuid16 == BLE_CHR_SSID_UUID) {
      return os_mbuf_append(ctxt->om, s_wifi_ssid, strlen(s_wifi_ssid));
    } else if (uuid16 == BLE_CHR_PASS_UUID) {
      return os_mbuf_append(ctxt->om, s_wifi_pass, strlen(s_wifi_pass));
    } else if (uuid16 == BLE_CHR_SENSOR_DATA_UUID) {
      return os_mbuf_append(ctxt->om, s_sensor_json, strlen(s_sensor_json));
    } else if (uuid16 == BLE_CHR_NODE_ID_UUID) {
      return os_mbuf_append(ctxt->om, &s_node_id, sizeof(s_node_id));
    }
    return BLE_ATT_ERR_UNLIKELY;

  case BLE_GATT_ACCESS_OP_WRITE_CHR: {
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

    if (uuid16 == BLE_CHR_SSID_UUID) {
      if (len >= BLE_MAX_SSID_LEN)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      memset(s_wifi_ssid, 0, sizeof(s_wifi_ssid));
      os_mbuf_copydata(ctxt->om, 0, len, s_wifi_ssid);
      ESP_LOGI(TAG, "BLE da nhan SSID: %s", s_wifi_ssid);
    } else if (uuid16 == BLE_CHR_PASS_UUID) {
      if (len >= BLE_MAX_PASS_LEN)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      memset(s_wifi_pass, 0, sizeof(s_wifi_pass));
      os_mbuf_copydata(ctxt->om, 0, len, s_wifi_pass);
      ESP_LOGI(TAG, "BLE da nhan PASSWORD: %s", s_wifi_pass);

      if (strlen(s_wifi_ssid) > 0 && s_config_cb) {
        s_config_cb(s_wifi_ssid, s_wifi_pass);
      }
    } else if (uuid16 == BLE_CHR_NODE_ID_UUID) {
      if (len != 1)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      os_mbuf_copydata(ctxt->om, 0, 1, &s_node_id);
      nvs_save_node_id(s_node_id);
      ESP_LOGI(TAG, "BLE da cap nhat Node ID moi: %d", s_node_id);
    }
    return 0;
  }

  default:
    return BLE_ATT_ERR_UNLIKELY;
  }
}

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      s_conn_handle = event->connect.conn_handle;
      s_is_connected = true;
      ESP_LOGI(TAG, "Thiet bi (App/Gateway) da ket noi BLE! ConnHandle=%d",
               s_conn_handle);
    } else {
      s_is_connected = false;
      if (!s_ble_stopping)
        ble_app_advertise();
    }
    break;

  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "Thiet bi da ngat ket noi BLE -> Tiep tuc phat quang ba...");
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_is_connected = false;
    if (!s_ble_stopping)
      ble_app_advertise();
    break;

  case BLE_GAP_EVENT_SUBSCRIBE:
    ESP_LOGI(TAG,
             "Client subscribe characteristic: attr_handle=%d, cur_notify=%d",
             event->subscribe.attr_handle, event->subscribe.cur_notify);
    break;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    if (!s_ble_stopping)
      ble_app_advertise();
    break;

  default:
    break;
  }
  return 0;
}

static void ble_app_advertise(void) {
  struct ble_gap_adv_params adv_params;
  struct ble_hs_adv_fields fields;
  int rc;

  memset(&fields, 0, sizeof(fields));
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.tx_pwr_lvl_is_present = 1;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
  fields.name = (uint8_t *)s_device_name;
  fields.name_len = strlen(s_device_name);
  fields.name_is_complete = 1;

  ble_uuid16_t svc_uuid = BLE_UUID16_INIT(BLE_SVC_NODE_UUID);
  fields.uuids16 = &svc_uuid;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

  rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "Loi thiet lap adv fields; rc=%d", rc);
    return;
  }

  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                         ble_gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Loi bat quang ba BLE; rc=%d", rc);
    return;
  }
  ESP_LOGI(TAG, "Dang phat quang ba BLE voi ten: [%s] ...", s_device_name);
}

static void ble_on_sync(void) {
  ble_hs_id_infer_auto(0, &s_own_addr_type);
  ble_app_advertise();
}

static void ble_host_task(void *param) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

esp_err_t ble_node_init(const char *device_name) {
  if (device_name && strlen(device_name) > 0) {
    strncpy(s_device_name, device_name, sizeof(s_device_name) - 1);
  }
  s_ble_stopping = false;

  nvs_load_node_id(&s_node_id);

  esp_err_t ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "nimble_port_init that bai (%d), khoi tao lai...", ret);
    nimble_port_stop();
    nimble_port_deinit();
    vTaskDelay(pdMS_TO_TICKS(150));
    ret = nimble_port_init();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Khoi tao NimBLE that bai: %d", ret);
      return ret;
    }
  }

  ble_svc_gap_device_name_set(s_device_name);
  ble_svc_gap_init();
  ble_svc_gatt_init();

  ble_gatts_count_cfg(s_gatt_svcs);
  ble_gatts_add_svcs(s_gatt_svcs);

  ble_hs_cfg.sync_cb = ble_on_sync;

  nimble_port_freertos_init(ble_host_task);
  s_ble_is_running = true;
  ESP_LOGI(TAG, "Khoi tao BLE GATT Server thanh cong!");
  return ESP_OK;
}

esp_err_t ble_node_update_sensor_data(float temp, float hum, bool presence) {
  snprintf(s_sensor_json, sizeof(s_sensor_json),
           "{\"temp\":%.2f,\"hum\":%.2f,\"presence\":%d}", temp, hum,
           presence ? 1 : 0);

  if (s_is_connected && s_sensor_val_handle != 0) {
    ble_gatts_chr_updated(s_sensor_val_handle);
  }
  return ESP_OK;
}

void ble_node_register_wifi_callback(ble_wifi_config_cb_t cb) {
  s_config_cb = cb;
}

bool ble_node_is_connected(void) { return s_is_connected; }

esp_err_t ble_node_stop(void) {
  if (!s_ble_is_running)
    return ESP_OK;
  s_ble_stopping = true;
  ble_gap_adv_stop();
  int rc = nimble_port_stop();
  s_ble_is_running = false;
  return (rc == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t ble_node_deinit(void) {
  if (!s_ble_is_running)
    return ESP_OK;
  s_ble_stopping = true;
  ble_gap_adv_stop();
  int rc = nimble_port_stop();
  s_ble_is_running = false;
  if (rc == 0) {
    vTaskDelay(pdMS_TO_TICKS(100));
    nimble_port_deinit();
    ESP_LOGI(TAG, "BLE Node deinitialized.");
    return ESP_OK;
  }
  return ESP_FAIL;
}
