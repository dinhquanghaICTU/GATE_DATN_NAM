# ESP32-C3 Super Mini Presence Node Firmware (DATN_NAM)

Dự án firmware cho **Node Cảm biến Hiện diện (Human Presence Node)** chạy trên board **ESP32-C3 Super Mini**, thuộc hệ thống Đồ Án Tốt Nghiệp. Node này có nhiệm vụ quét trạng thái người (qua radar mmWave / PIR), điều khiển tải đèn cục bộ và truyền dữ liệu không dây tốc độ cao về trạm Gateway trung tâm qua giao thức **ESP-NOW**.

---

## 1. Thông Tin Phần Cứng & Cấu Hình

- **Vi điều khiển:** ESP32-C3 (RISC-V 32-bit Single-Core up to 160MHz).
- **Bộ nhớ Flash:** 4MB Flash (DIO 80MHz).
- **Phân vùng bộ nhớ (Dual OTA):** `ota_0` (1.8MB) và `ota_1` (1.8MB) hỗ trợ nạp OTA an toàn kèm Rollback tự động.
- **Giao tiếp không dây:** ESP-NOW kết nối trực tiếp với trạm **Gateway S3 (GATE)**, không cần kết nối Router Wi-Fi để tiết kiệm điện năng và tối ưu độ trễ (<5ms).

---

## 2. Sơ Đồ Chân Phần Cứng (Hardware Pinout - ESP32-C3 Super Mini)

Cấu hình chi tiết có trong file [`middle/config.h`](file:///home/quanghaictu/DATN_NAM/firmware/PRESENCE/middle/config.h):

| Chân GPIO | Tên Ngoại Vi | Chức Năng |
| :---: | :--- | :--- |
| **GPIO 4** | **Cảm biến Hiện diện (Radar/PIR)** | Nhận tín hiệu logic (1 = Có người, 0 = Không có người) từ cảm biến (LD2410, RCWL, v.v.) |
| **GPIO 8** | **Onboard Blue LED** | Đèn báo trạng thái (Active LOW: Sáng khi có người, chớp nhẹ khi idle) |
| **GPIO 9** | **Onboard BOOT Button** | Nút bấm kiểm tra: Nhấn nhả để đảo Relay / Nhấn giữ 10s reboot |
| **GPIO 3** | **Relay / Bật Đèn** | Ngõ ra điều khiển đèn chiếu sáng trực tiếp khi có người |

---

## 3. Kiến Trúc Phân Tầng (Layered Architecture)

```text
firmware/PRESENCE/
├── CMakeLists.txt                 # Root CMake project
├── partitions.csv                 # Bảng phân vùng 4MB Dual OTA
├── sdkconfig.defaults             # Cấu hình mặc định cho ESP32-C3 (RISC-V)
├── esp.sh                         # Tool CLI tự động build, flash, monitor
├── .vscode/                       # Cấu hình RISC-V IntelliSense cho VS Code
├── main/
│   ├── CMakeLists.txt
│   └── main.c                     # app_main khởi tạo phần cứng & tasks
├── application/
│   ├── CMakeLists.txt
│   └── presence_controller/       # Xử lý logic hiện diện, đóng ngắt relay, gửi ESP-NOW
├── middle/
│   ├── CMakeLists.txt
│   ├── config.h                   # Định nghĩa chân GPIO, Node ID, Channel ESP-NOW
│   ├── wifi/                      # Wi-Fi STA channel 1 phục vụ ESP-NOW
│   └── flash/                     # Lưu trữ cấu hình NVS Flash
├── hardware/
│   ├── CMakeLists.txt
│   ├── presence_sensor/           # Driver cảm biến hiện diện (quét logic, tính thời gian)
│   ├── led/                       # Driver LED xanh onboard GPIO 8
│   ├── button/                    # Driver nút bấm BOOT GPIO 9
│   ├── relay/                     # Driver điều khiển Relay GPIO 3
│   └── esp_now_node/              # Giao tiếp thu phát ESP-NOW với Gateway
└── third_party/
    ├── CMakeLists.txt
    ├── button/                    # Thư viện debounce nút bấm app_btn
    └── jsmn/                      # Parser JSON
```

---

## 4. Cách Sử Dụng & Biên Dịch

### 4.1. Sử dụng script `esp.sh` (Khuyên dùng)
```bash
cd /home/quanghaictu/DATN_NAM/firmware/PRESENCE

# Mở menu điều khiển tương tác trực quan:
./esp.sh

# Hoặc dùng các lệnh nhanh:
./esp.sh build              # Biên dịch mã nguồn (RISC-V)
./esp.sh flash              # Tự nhận cổng và nạp firmware
./esp.sh monitor            # Mở Serial Monitor
./esp.sh flash-monitor      # Vừa nạp vừa mở log
./esp.sh clean              # Dọn dẹp bản build
```

### 4.2. Dùng lệnh gốc `idf.py`
```bash
get_idf  # (hoặc: . $HOME/esp/esp-idf/export.sh)

cd /home/quanghaictu/DATN_NAM/firmware/PRESENCE

# Đặt target cho chip ESP32-C3
idf.py set-target esp32c3

# Biên dịch
idf.py build

# Nạp và xem log
idf.py -p /dev/ttyACM0 flash monitor
```
