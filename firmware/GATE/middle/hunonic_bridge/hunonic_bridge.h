#ifndef __HUNONIC_BRIDGE_H__
#define __HUNONIC_BRIDGE_H__

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_HUNONIC_AC_DEVICES 4

/**
 * @brief Cấu trúc dữ liệu đại diện cho 1 chiếc điều hòa Hunonic
 */
typedef struct {
  char id[16];         // Ví dụ: "3992007"
  char name[32];       // Ví dụ: "Điều hòa FUNIKI"
  int brand;           // Mã hãng: 104 (Funiki), 101 (Daikin)...
  char topic_sub[128]; // Topic nhận lệnh của trạm IR Pro
  char topic_pub[128]; // Topic phản hồi của trạm IR Pro
  int power;           // 0: Tắt, 1: Bật
  int temp;            // Nhiệt độ: 16 - 30
  int mode;            // 1: Cool, 2: Heat, 3: Dry, 4: Fan, 0: Auto
  int fan;             // 0: Auto, 1: Min, 3: Med, 5: Max
  bool is_valid;       // Đánh dấu slot này có thiết bị hợp lệ
} hunonic_ac_device_t;

/**
 * @brief Callback khi điều hòa có cập nhật trạng thái mới (từ WebSocket hoặc
 * sync)
 */
typedef void (*hunonic_ac_state_cb_t)(const hunonic_ac_device_t *device,
                                      void *arg);

/**
 * @brief Khởi tạo Bridge: kiểm tra Token trong NVS (nếu chưa có hoặc hết hạn
 * thì tự động HTTP POST login), đồng bộ danh sách thiết bị và kết nối WebSocket
 * ngầm.
 * @param phone Số điện thoại đăng nhập
 * @param password Mật khẩu tài khoản (dạng chuỗi rõ hoặc MD5)
 * @return esp_err_t ESP_OK nếu khởi tạo thành công
 */
esp_err_t hunonic_bridge_init(const char *phone, const char *password);

/**
 * @brief Đăng ký callback nhận thông báo khi điều hòa thay đổi trạng thái
 */
void hunonic_bridge_register_state_cb(hunonic_ac_state_cb_t cb, void *arg);

/**
 * @brief Gọi HTTP GET listDeviceByHome để làm mới danh sách thiết bị
 */
esp_err_t hunonic_bridge_sync_devices(void);

/**
 * @brief Lấy con trỏ danh sách điều hòa hiện có trên Gateway
 */
const hunonic_ac_device_t *hunonic_bridge_get_devices(int *out_count);

/**
 * @brief Bật / Tắt điều hòa qua WebSocket
 * @param device_id ID của điều hòa (ví dụ "3992007")
 * @param power true = BẬT, false = TẮT
 */
esp_err_t hunonic_bridge_set_power(const char *device_id, bool power);

/**
 * @brief Đặt nhiệt độ điều hòa qua WebSocket
 * @param device_id ID của điều hòa (ví dụ "3992007")
 * @param temp Nhiệt độ mong muốn (ví dụ 25)
 */
esp_err_t hunonic_bridge_set_temp(const char *device_id, int temp);

/**
 * @brief Gửi lệnh điều khiển đầy đủ các thông số
 */
esp_err_t hunonic_bridge_control(const char *device_id, int power, int temp,
                                 int mode, int fan);

/**
 * @brief Kiểm tra xem WebSocket điều khiển có đang kết nối không
 */
bool hunonic_bridge_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* __HUNONIC_BRIDGE_H__ */
