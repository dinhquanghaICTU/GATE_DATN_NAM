# PROJECT GUIDELINES: ESP32-S3 Gateway Firmware (DATN_NAM)

Tài liệu này cung cấp toàn bộ hướng dẫn, tiêu chuẩn lập trình, quy tắc phần cứng và quy trình build/flash cho trợ lý AI (và lập trình viên) khi phát triển mã nguồn trong dự án này.

---

## 1. Thông Tin Dự Án & Phần Cứng

- **Mục tiêu:** Trạm Gateway trung tâm thu thập dữ liệu node không dây và giao tiếp Cloud.
- **Chip:** ESP32-S3 (Xtensa Dual-Core 32-bit LX7, up to 240 MHz).
- **Phân bản bộ nhớ:** Flash 16MB (hỗ trợ Dual OTA + Rollback + Storage), PSRAM 8MB.
- **Framework / SDK:** Official **ESP-IDF v5.3+** (C / C++).
- **Hệ điều hành RTOS:** FreeRTOS (ESP-IDF flavor).

---

## 2. Quy Trình Dòng Lệnh (CLI Workflow)

```bash
# 1. Kích hoạt môi trường ESP-IDF
get_idf
# Hoặc: . $HOME/esp/esp-idf/export.sh

# 2. Điều hướng vào thư mục Gateway
cd /home/quanghaictu/DATN_NAM/firmware/GATE

# 3. Sử dụng công cụ tự động hóa esp.sh
./esp.sh build
./esp.sh flash
./esp.sh monitor
# Hoặc tương đương qua idf.py:
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

---

## 3. Kiến Trúc Phân Tầng (Layered Architecture)

- **`main`**: Khởi chạy hệ thống, xác thực phân vùng OTA đang chạy và hủy rollback nếu boot hợp lệ, khởi tạo FreeRTOS tasks.
- **`application`**:
  - `gate_controller`: Điều phối gói tin từ ESP-NOW lên Cloud qua MQTT và điều hướng lệnh xuống node/actuator.
  - `m_state_machine`: Quản lý chu trình kết nối mạng (BLE provisioning -> Wi-Fi -> MQTT -> Online -> OTA).
- **`middle`**: Tầng giao thức kết nối (`wifi`, `mqtt`, `ble`, `flash`, `ota`).
- **`hardware`**: Tầng ngoại vi phần cứng (`led`, `button`, `relay`, `esp_now_node`).
- **`third_party`**: Các thư viện tối ưu hóa không cấp phát động (`jsmn`, `app_btn`).

---

## 4. Quy Tắc & Tiêu Chuẩn Lập Trình (Coding Standards)

1. **Logging**: Sử dụng `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`, `ESP_LOGD` từ thư viện `esp_log.h`. Tuyệt đối không dùng `printf()` bừa bãi.
2. **Bộ Nhớ & RTOS**:
   - Sử dụng stack hợp lý cho các task (từ 2048 - 4096 bytes).
   - Vòng lặp vô tận của các task FreeRTOS **luôn phải có** `vTaskDelay` hoặc cơ chế block semaphore/queue để nhường CPU.
3. **Quản lý CMake**:
   - Mỗi khi bổ sung file `.c` hoặc thư mục con trong `application/`, `hardware/`, `middle/`, AI phải tự động cập nhật danh sách `SRCS` và `INCLUDE_DIRS` trong `CMakeLists.txt` tương ứng.
4. **Không can thiệp build cache**: Không sửa đổi thủ công thư mục `build/`.
