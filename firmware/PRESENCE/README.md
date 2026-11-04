# ESP32-C3 Super Mini Presence Node Firmware (DATN_NAM)

Dự án firmware cho **Node Cảm biến Hiện diện (Human Presence Node)** chạy trên board **ESP32-C3 Super Mini**, thuộc hệ thống Đồ Án Tốt Nghiệp. Node này có nhiệm vụ quét trạng thái người (qua radar LD2410B UART), điều khiển tải đèn cục bộ và truyền dữ liệu không dây tốc độ cao về trạm Gateway trung tâm qua giao thức **ESP-NOW**.

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
| **GPIO 8** | **I2C SDA** | Chân dữ liệu I2C cảm biến BMP280 / SHT30 (Fallback tự động GPIO 6) |
| **GPIO 9** | **I2C SCL** | Chân xung clock I2C cảm biến BMP280 / SHT30 (Fallback tự động GPIO 7) |
| **GPIO 20 (RX)** | **LD2410B UART_TX** | Nhận dữ liệu radar qua UART1, 256000 baud, 8N1 |
| **GPIO 21 (TX)** | **LD2410B UART_RX** | Gửi dữ liệu đến radar |
| **GPIO 3** | **Relay / Tải Đèn** | Điều khiển bật/tắt tải đèn (Tự động bật khi có người hoặc thủ công) |

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
│   ├── bmp280/                    # Driver cảm biến nhiệt độ & áp suất khí quyển BMP280 / BME280
│   ├── SHT30/                     # Driver cảm biến nhiệt độ & độ ẩm SHT30 (Dự phòng)
│   ├── presence_sensor/           # Driver LD2410B UART, parser frame, thời gian hiện diện
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

## LD2410B: đấu dây và kiểm tra

- Radar VCC → 5V ổn định (nguồn có khả năng cấp trên 200mA), GND → GND chung.
- Radar TX → GPIO20; radar RX → GPIO21. OUT không sử dụng.
- ESP32-C3 dùng UART1 ở 256000 baud, 8N1; không tự đổi cấu hình hoặc baud của radar.
- Console dùng USB Serial/JTAG, cắm USB trực tiếp C3 và chọn `/dev/ttyACM*`. Không dùng console UART0 trên GPIO20/21 vì đang dành cho radar.
- `sdkconfig.defaults` cấu hình console cho build mới; cấu hình `sdkconfig` hiện tại cũng đã chuyển sang USB.
- Log `[LD2410B TEST]` mỗi 2 giây hiển thị trạng thái, khoảng cách, năng lượng, số frame và tuổi dữ liệu. Số frame tăng và dữ liệu còn mới cho biết UART đang nhận.
- Driver kiểm tra header/footer, độ dài frame và tự tìm lại frame sau byte rác; hỗ trợ bản tin thường và engineering.
- Mất bản tin quá 2 giây được đánh dấu offline, không tự suy ra phòng trống. Hold-off của firmware là 4 giây sau chuỗi bản tin không có người, ngoài thời gian trễ đã đặt trong radar.

## ESP-NOW: đồng bộ kênh với Gateway

Node bắt đầu trên `ESP_NOW_WIFI_CHANNEL`, sau mỗi unicast NACK sẽ thử kênh kế tiếp trong chu kỳ telemetry sau, theo miền Wi-Fi được cấu hình. Khi có MAC ACK, node giữ kênh đó. Gateway giữ kênh router đang kết nối; không ép Gate về kênh 1 khi dùng Wi-Fi Cloud. Một vòng dò có thể mất vài chục giây.

Đối chiếu `GATEWAY_MAC_ADDR` với log `ESP-NOW STA MAC=... channel=...` bên Gate. MAC ACK chỉ xác nhận tầng radio; cần log `[ESP-NOW RECV]` bên Gate để xác nhận xử lý bản tin. Broadcast TX thành công không xác nhận Gateway đã nhận.
