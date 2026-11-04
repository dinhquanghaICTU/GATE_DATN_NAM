#ifndef __HUNONIC_BRIDGE_H__
#define __HUNONIC_BRIDGE_H__

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Keep this bounded for predictable RAM use, but do not make adding a new AC
 * require a firmware change. */
#define MAX_HUNONIC_AC_DEVICES 16

typedef struct {
  char id[16];
  /* The IR hub that owns this child.  It is required when an account has
   * more than one IR hub. */
  /* Hunonic may supply an opaque, encrypted routing ID for an IR child. */
  char root_id[256];
  bool root_id_is_encrypted;
  char name[32];
  int brand;
  char topic_sub[128];
  char topic_pub[128];
  int power;
  int temp;
  int mode;
  int fan;
  bool is_valid;
} hunonic_ac_device_t;

typedef void (*hunonic_ac_state_cb_t)(const hunonic_ac_device_t *device,
                                      void *arg);

esp_err_t hunonic_bridge_init(const char *phone, const char *password);

void hunonic_bridge_register_state_cb(hunonic_ac_state_cb_t cb, void *arg);

esp_err_t hunonic_bridge_sync_devices(void);

const hunonic_ac_device_t *hunonic_bridge_get_devices(int *out_count);

esp_err_t hunonic_bridge_set_power(const char *device_id, bool power);

esp_err_t hunonic_bridge_set_temp(const char *device_id, int temp);

esp_err_t hunonic_bridge_control(const char *device_id, int power, int temp,
                                 int mode, int fan);

bool hunonic_bridge_is_connected(void);

bool hunonic_bridge_is_ir_hub_online(void);

esp_err_t hunonic_bridge_stop(void);

#ifdef __cplusplus
}
#endif

#endif

