# ESP32-S3 Gateway Firmware (DATN_NAM)

Dự án firmware trạm thu phát Gateway trung tâm dựa trên chip **ESP32-S3**, phục vụ Đồ Án Tốt Nghiệp (DATN). Dự án được thiết kế theo kiến trúc phân tầng chuyên nghiệp (Layered Architecture), kế thừa và tối ưu hóa từ nền tảng firmware IoT thực tế.

---

## 1. Tính Năng Nổi Bật

- **Phần cứng mục tiêu:** ESP32-S3 (Xtensa Dual-Core 240MHz, 16MB Flash, 8MB PSRAM).
- **Kiến trúc phân tầng (Layered Architecture):**
  - `main`: Entry point, boot banner, OTA rollback verification, khởi tạo luồng tác vụ FreeRTOS.
  - `application`: Xử lý nghiệp vụ Gateway (`gate_controller`), máy trạng thái trung tâm (`m_state_machine`).
  - `middle`: Quản lý kết nối Wi-Fi Station (`wifi`), MQTT Client MQTTS/TCP (`mqtt`), cấu hình Wi-Fi qua BLE NimBLE (`ble`), lưu trữ NVS (`flash`), cập nhật OTA an toàn kép (`ota`).
  - `hardware`: Điều khiển LED RGB WS2812 (`led`), nút bấm chống rung (`button`), relay/actuator (`relay`), thu phát không dây ESP-NOW tốc độ cao với các node cảm biến (`esp_now_node`).
  - `third_party`: Thư viện xử lý chuỗi JSON không cấp phát động (`jsmn`), thư viện sự kiện nút bấm đa chức năng (`app_btn`).
- **An toàn cập nhật OTA kép (Dual OTA + Rollback):**
  - Tự động kiểm tra tính hợp lệ của bản cập nhật sau khi nạp OTA.
  - Nếu bản mới crash hoặc không chạy đúng, bootloader tự động Rollback về phiên bản trước đó mà không làm brick thiết bị.
- **Cấu hình Wi-Fi linh hoạt qua Bluetooth BLE (NimBLE):**
  - Bật chế độ cấu hình khi chưa có Wi-Fi hoặc khi nhấn giữ nút 5 giây.
  - Nhẹ, tốn cực ít RAM so với Bluedroid.
- **Công cụ dòng lệnh tự động hóa (`esp.sh`):**
  - Tự động nhận diện cổng USB (`/dev/ttyACM*`, `/dev/ttyUSB*`).
  - Menu tương tác trực quan hoặc chạy lệnh trực tiếp (build, flash, monitor, clean, menuconfig).

---

## 2. Cấu Trúc Thư Mục

```text
firmware/GATE/
├── CMakeLists.txt             # Root CMake project
├── partitions.csv             # Bảng phân vùng Dual OTA 16MB + Rollback
├── sdkconfig.defaults         # Cấu hình mặc định ESP-IDF cho ESP32-S3
├── esp.sh                     # Bộ công cụ build/flash CLI tiện lợi
├── .vscode/                   # Cấu hình VS Code IntelliSense & Clangd
├── main/                      # Entry point & cấu hình toàn cục
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   ├── config.h               # Định nghĩa chân GPIO, MQTT Broker, Topics
│   └── main.c                 # app_main khởi chạy hệ thống
├── application/               # Tầng nghiệp vụ Gateway
│   ├── CMakeLists.txt
│   ├── gate_controller/       # Thu thập dữ liệu node & forward lên Cloud
│   └── m_state_machine/       # Máy trạng thái kết nối & vận hành
├── middle/                    # Tầng giao thức & middleware
│   ├── CMakeLists.txt
│   ├── wifi/                  # Quản lý Wi-Fi Station & tự động kết nối lại
│   ├── mqtt/                  # MQTT Client (MQTTS TLS / TCP)
│   ├── ble/                   # BLE NimBLE Provisioning Wi-Fi
│   ├── flash/                 # Lưu trữ NVS Flash
│   └── ota/                   # HTTPS OTA Engine
├── hardware/                  # Tầng driver ngoại vi phần cứng
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   ├── led/                   # Driver LED RGB WS2812 đa trạng thái
│   ├── button/                # Driver nút bấm (Nhấn nhả, giữ 5s, giữ 10s)
│   ├── relay/                 # Driver điều khiển Relay
│   └── esp_now_node/          # Thu nhận ESP-NOW từ các node cảm biến
└── third_party/               # Các thư viện mã nguồn mở nhúng
    ├── CMakeLists.txt
    ├── jsmn/                  # Parser JSON siêu nhẹ, không tốn heap
    └── button/                # Engine xử lý sự kiện nút bấm app_btn
```

---

## 3. Sơ Đồ Chân Phần Cứng (Hardware Pinout)

Có thể tùy chỉnh lại dễ dàng trong file [`main/config.h`](file:///home/quanghaictu/DATN_NAM/firmware/GATE/main/config.h):

| Thiết Bị Ngoại Vi | Chân GPIO ESP32-S3 | Chức Năng |
| :--- | :---: | :--- |
| **LED RGB WS2812** | `GPIO 48` | Báo trạng thái hệ thống bằng màu sắc |
| **Nút bấm đa năng** | `GPIO 21` | Nhấn nhả: Đảo relay / Giữ 5s: Reset Wi-Fi / Giữ 10s: Factory Reset |
| **Relay 1 (Kênh chính)** | `GPIO 38` | Đóng cắt thiết bị 1 |
| **Relay 2 (Kênh phụ)** | `GPIO 39` | Đóng cắt thiết bị 2 |

---

## 4. Hướng Dẫn Sử Dụng & Biên Dịch

### 4.1. Sử dụng script `esp.sh` (Khuyên dùng)
```bash
cd /home/quanghaictu/DATN_NAM/firmware/GATE

# Mở menu tương tác:
./esp.sh

# Hoặc dùng các lệnh nhanh:
./esp.sh build              # Biên dịch mã nguồn
./esp.sh flash              # Nạp firmware vào cổng serial tự động nhận diện
./esp.sh monitor            # Mở Serial Monitor
./esp.sh flash-monitor      # Vừa nạp vừa mở log ngay
./esp.sh clean              # Dọn dẹp bản build cũ
```

### 4.2. Sử dụng lệnh gốc `idf.py`
```bash
# Nạp môi trường ESP-IDF
get_idf  # hoặc: . $HOME/esp/esp-idf/export.sh

cd /home/quanghaictu/DATN_NAM/firmware/GATE

# Đặt target cho chip ESP32-S3
idf.py set-target esp32s3

# Build
idf.py build

# Flash & Monitor
idf.py -p /dev/ttyACM0 flash monitor
```

---

## 5. MQTT Topics & JSON Định Dạng

- **Topic nhận lệnh điều khiển:** `gate/command`
  - Đảo relay: `{"cmd":"relay","ch":1,"state":1}`
  - Gửi lệnh xuống Node: `{"cmd":"node_cmd","node_id":1,"cmd_id":2,"param":100}`
  - Khởi động lại: `{"cmd":"reboot"}`
  - Nạp OTA: `{"cmd":"ota","url":"https://example.com/firmware.bin","version":"1.0.1","target":"esp32s3_gate"}`
- **Topic đẩy dữ liệu cảm biến node:** `gate/telemetry`
  - Ví dụ: `{"node_id":1,"packet_id":102,"mac":"34:85:18:XX:XX:XX","val1":27.8,"val2":64.5,"battery":4.12,"fw":"1.0.0","rssi":-58}`
- **Topic trạng thái định kỳ Gateway (Heartbeat):** `gate/status`
  - Ví dụ: `{"event":"heartbeat","device":"esp32s3_gate","uptime_sec":120,"free_heap":235120,"rssi":-52,"total_node_pkts":38,"fw_ver":"1.0.0"}`
