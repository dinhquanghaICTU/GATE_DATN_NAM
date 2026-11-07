# PROJECT GUIDELINES: Embedded Firmware (DATN_NAM)

Tài liệu này cung cấp toàn bộ hướng dẫn, tiêu chuẩn lập trình, quy tắc phần cứng và quy trình build/flash cho trợ lý AI (và lập trình viên) khi phát triển mã nguồn trong toàn bộ hệ thống DATN_NAM.

---

# PHẦN 1: GATEWAY FIRMWARE (ESP32-S3)

## 1. Thông Tin Dự Án & Phần Cứng
- **Mục tiêu:** Trạm Gateway trung tâm thu thập dữ liệu node không dây và giao tiếp Cloud.
- **Thư mục:** `firmware/GATE`
- **Chip:** ESP32-S3 (Xtensa Dual-Core 32-bit LX7, up to 240 MHz).
- **Phân bản bộ nhớ:** Flash 16MB (hỗ trợ Dual OTA + Rollback + Storage), PSRAM 8MB.
- **Framework / SDK:** Official **ESP-IDF v5.3+** (C / C++).
- **Hệ điều hành RTOS:** FreeRTOS (ESP-IDF flavor).

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

## 3. Kiến Trúc Phân Tầng (Layered Architecture)
- **`main`**: Khởi chạy hệ thống, xác thực phân vùng OTA đang chạy và hủy rollback nếu boot hợp lệ, khởi tạo FreeRTOS tasks.
- **`application`**:
  - `gate_controller`: Điều phối gói tin từ ESP-NOW lên Cloud qua MQTT và điều hướng lệnh xuống node/actuator.
  - `m_state_machine`: Quản lý chu trình kết nối mạng (BLE provisioning -> Wi-Fi -> MQTT -> Online -> OTA).
- **`middle`**: Tầng giao thức kết nối (`wifi`, `mqtt`, `ble`, `flash`, `ota`, `hunonic_bridge`).
- **`hardware`**: Tầng ngoại vi phần cứng (`led`, `button`, `relay`, `esp_now_node`).
- **`third_party`**: Các thư viện tối ưu hóa không cấp phát động (`jsmn`, `app_btn`).

## 4. Quy Tắc & Tiêu Chuẩn Lập Trình (Coding Standards)
1. **Logging**: Sử dụng `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`, `ESP_LOGD` từ thư viện `esp_log.h`. Tuyệt đối không dùng `printf()` bừa bãi.
2. **Bộ Nhớ & RTOS**:
   - Sử dụng stack hợp lý cho các task (từ 2048 - 4096 bytes).
   - Vòng lặp vô tận của các task FreeRTOS **luôn phải có** `vTaskDelay` hoặc cơ chế block semaphore/queue để nhường CPU.
3. **Quản lý CMake**:
   - Mỗi khi bổ sung file `.c` hoặc thư mục con trong `application/`, `hardware/`, `middle/`, AI phải tự động cập nhật danh sách `SRCS` và `INCLUDE_DIRS` trong `CMakeLists.txt` tương ứng.
4. **Không can thiệp build cache**: Không sửa đổi thủ công thư mục `build/`.

---

# PHẦN 2: POWER QUALITY FIRMWARE (STM32F103C8T6)

## 1. Thông Tin Dự Án & Phần Cứng
- **Mục tiêu:** Đo lường giám sát chất lượng điện năng, dòng điện, điện áp, công suất và gửi cảnh báo/dữ liệu về Gateway.
- **Thư mục:** `firmware/POWER_QUALITY`
- **Chip:** STM32F103C8T6 (ARM Cortex-M3 32-bit @ 72 MHz).
- **Bộ nhớ:** 64 KB Flash, 20 KB SRAM.
- **Library / SDK:** **STM32F10x Standard Peripheral Library (StdPeriph v3.5.0)** + ARM CMSIS Core (Chế độ hiện tại: Bare-metal Super Loop; FreeRTOS tích hợp sẵn trong `Middlewares/` dự phòng khi cần).
- **Toolchain:** `arm-none-eabi-gcc` + `make` / `cmake`.

## 2. Quy Trình Dòng Lệnh (CLI Workflow)
```bash
# Điều hướng vào thư mục
cd /home/quanghaictu/DATN_NAM/firmware/POWER_QUALITY

# Dùng công cụ tự động hóa run.sh (hoặc stm32.sh):
./run.sh                    # Mở Menu tương tác điều khiển đầy đủ (TUI Menu)
./run.sh all [port] [baud]  # Combo: Build + Nạp + Mở Serial Log UART ngay
./run.sh build              # Biên dịch firmware (tự động cập nhật compile_commands.json)
./run.sh rebuild            # Clean và biên dịch lại từ đầu
./run.sh flash              # Tự động nạp qua ST-Link (st-flash hoặc OpenOCD)
./run.sh size               # Phân tích % dung lượng Flash ROM & SRAM chiếm dụng
./run.sh erase              # Xóa sạch toàn bộ chip (Full Mass Erase)
./run.sh minicom [port]     # Mở Serial Monitor xem log UART (115200)
./run.sh fix-usb            # Cấp quyền udev vĩnh viễn cho ST-Link/UART
./run.sh clean              # Xóa thư mục build/

# Hoặc dùng trực tiếp make:
make -j$(nproc)
make clean
```

## 3. Cấu Trúc Dự Án (Project Layout)
- **`Core/`**:
  - `Core/Inc/`: `main.h`, `FreeRTOSConfig.h` (cấu hình FreeRTOS), `stm32f10x_conf.h` (cấu hình ngoại vi StdPeriph), `stm32f10x_it.h`.
  - `Core/Src/`: `main.c` (khởi tạo FreeRTOS tasks), `stm32f10x_it.c` (ngắt hệ thống), `system_stm32f10x.c` (khởi tạo 72MHz).
- **`Drivers/`**:
  - `CMSIS/Core/`: `core_cm3.c`, `core_cm3.h` (nhân ARM Cortex-M3, đã patch early-clobber cho GCC).
  - `CMSIS/Device/`: `stm32f10x.h`, `system_stm32f10x.h`, `Startup/startup_stm32f10x_md.s` (startup vector medium density).
  - `STM32F10x_StdPeriph_Driver/`: Toàn bộ thư viện chuẩn StdPeriph v3.5.0 (ADC, GPIO, RCC, USART, SPI, I2C, TIM, DMA, EXTI, FLASH,...).
- **`Middlewares/Third_Party/FreeRTOS/`**:
  - `Source/`: `tasks.c`, `queue.c`, `list.c`, `timers.c`, `event_groups.c`, `stream_buffer.c`.
  - `Source/portable/`: `port.c` (GCC ARM_CM3), `heap_4.c` (cấp phát động).
- **`middle/`**: Các module giao thức truyền thông / debug trung gian:
  - `middle/uart_debug/`: Khởi tạo USART1 (PA9/PA10), gửi chuỗi/byte, ring buffer nhận ngắt an toàn với FreeRTOS, redirect hàm `printf()` chuẩn.
- **`third_party/`**: Thư viện bên ngoài tối ưu cho vi điều khiển:
  - `third_party/ringbuff/`: Bộ đệm vòng Ring Buffer của Tilen Majerle.
  - `third_party/jsmn/`: Bộ giải mã JSON siêu nhẹ không cấp phát động.
- **`Hardware/`**: Các module ngoại vi board:
  - `Hardware/led/`: Điều khiển LED PC13 (Active-Low) trên Blue Pill board.
  - `Hardware/delay/`: Hàm `delay_ms()` (tự động chuyển sang `vTaskDelay` khi RTOS chạy) và `delay_us()`.
  - `Hardware/pzem/`: Module cảm biến công suất PZEM-004T.
- **`STM32F103C8Tx_FLASH.ld`**: Linker script phân bổ bộ nhớ cho Flash 64K (`0x08000000`) và RAM 20K (`0x20000000`, `_estack = 0x20005000`).
- **`stm32.sh` & `Makefile`**: Script dòng lệnh chuẩn và Makefile tối ưu cho ARM GCC.

## 4. Tiêu Chuẩn & Quy Tắc Lập Trình Cho STM32
1. **Defines bắt buộc trong Compiler:**
   - `-DSTM32F10X_MD`: Xác định dòng Medium-Density cho STM32F103C8.
   - `-DUSE_STDPERIPH_DRIVER`: Kích hoạt driver StdPeriph và nạp file `stm32f10x_conf.h`.
2. **Cấu hình xung nhịp (Clock):**
   - Đã cấu hình HSE 8MHz qua PLL nhân 9 để ra tần số lõi 72 MHz (SYSCLK = 72MHz, HCLK = 72MHz, APB1 = 36MHz, APB2 = 72MHz).
3. **Thêm Driver / Module mới:**
   - Đặt module phần cứng độc lập vào thư mục `Hardware/<ten_module>/`.
   - Cập nhật biến `C_SOURCES` và `C_INCLUDES` trong [Makefile](file:///home/quanghaictu/DATN_NAM/firmware/POWER_QUALITY/Makefile) để `make` tự động biên dịch.
