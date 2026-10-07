#include "flash.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NAMESPACE "node_cfg"
static const char *TAG = "MIDDLE_FLASH";

esp_err_t nvs_save_node_id(uint8_t id) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = nvs_set_u8(handle, "node_id", id);
  if (err == ESP_OK) {
    nvs_commit(handle);
    ESP_LOGI(TAG, "Da luu Node ID: %d vao NVS!", id);
  }
  nvs_close(handle);
  return err;
}

esp_err_t nvs_load_node_id(uint8_t *id) {
  if (!id) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) return err;
  err = nvs_get_u8(handle, "node_id", id);
  nvs_close(handle);
  return err;
}

esp_err_t nvs_save_interval(uint32_t interval_ms) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = nvs_set_u32(handle, "interval", interval_ms);
  if (err == ESP_OK) nvs_commit(handle);
  nvs_close(handle);
  return err;
}

esp_err_t nvs_load_interval(uint32_t *interval_ms) {
  if (!interval_ms) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) return err;
  err = nvs_get_u32(handle, "interval", interval_ms);
  nvs_close(handle);
  return err;
}
