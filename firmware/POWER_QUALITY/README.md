# STM32F103C8T6 Power Quality Node Firmware (DATN_NAM)

Dự án firmware cho Node đo đạc và giám sát chất lượng điện năng (Power Quality / Công tơ điện thông minh) trong đồ án tốt nghiệp ICTU, sử dụng vi điều khiển **STM32F103C8T6**, hệ điều hành thời gian thực **FreeRTOS Kernel** và thư viện chuẩn **Standard Peripheral Library (StdPeriph v3.5.0)**.

---

## 1. Thông Số Phần Cứng (Hardware Specs)

* **Chip:** STM32F103C8T6 (ARM Cortex-M3 32-bit).
* **Tần số hoạt động (Core Clock):** 72 MHz (HSE thạch anh ngoài 8MHz qua PLL).
* **Bộ nhớ:** Flash 64 KB, SRAM 20 KB.
* **RTOS:** FreeRTOS (Preemption, Heap_4: 10 KB, Tick: 1000 Hz / 1ms).
* **Mục đích:** Thu thập dữ liệu đo điện (P, Q, S, U, I, cosφ, kWh, sóng hài THD), giao tiếp Modbus RS485 / UART với Edge Gateway ESP32-S3.
* **LED trạng thái mặc định:** Chân `PC13` (Active LOW trên board Blue Pill).

---

## 2. Kiến Trúc Thư Mục (Directory Architecture)

```text
firmware/POWER_QUALITY/
├── Core/
│   ├── Inc/
│   │   ├── main.h               # Header chính
│   │   ├── FreeRTOSConfig.h     # Cấu hình FreeRTOS (Heap, Tick, Priorities, ISRs)
│   │   ├── stm32f10x_conf.h     # Bật/tắt ngoại vi StdPeriph
│   │   └── stm32f10x_it.h       # Khai báo ngắt hệ thống
│   └── Src/
│       ├── main.c               # Khởi tạo FreeRTOS Task nháy LED & Start Scheduler
│       ├── stm32f10x_it.c       # Trình xử lý ngắt (Interrupt Handlers)
│       └── system_stm32f10x.c   # Khởi tạo Clock hệ thống 72MHz
├── Drivers/
│   ├── CMSIS/
│   │   ├── Core/                # Cortex-M3 Core (core_cm3.c, core_cm3.h)
│   │   └── Device/              # Register mapping (stm32f10x.h) & Startup (.s)
│   └── STM32F10x_StdPeriph_Driver/ # Toàn bộ driver ngoại vi StdPeriph (GPIO, RCC, USART, TIM, ADC, DMA, I2C, SPI,...)
├── Middlewares/
│   └── Third_Party/
│       └── FreeRTOS/            # Mã nguồn chính hãng FreeRTOS Kernel
│           └── Source/
│               ├── include/     # FreeRTOS headers (task.h, queue.h, semphr.h...)
│               ├── portable/    # Port Cortex-M3 (port.c, portmacro.h, heap_4.c)
│               └── *.c          # tasks.c, queue.c, list.c, timers.c...
├── middle/                      # Tầng giao thức & debug trung gian
│   └── uart_debug/              # Debug USART1 qua ringbuffer & retarget printf()
├── third_party/                 # Thư viện ngoài nhẹ cho MCU
│   ├── ringbuff/                # Ring buffer của Tilen Majerle
│   └── jsmn/                    # JSON Parser siêu nhẹ không cấp phát động
├── Hardware/                    # Tầng driver ngoại vi phần cứng mở rộng
│   ├── led/                     # Điều khiển LED báo trạng thái (PC13)
│   ├── delay/                   # Delay FreeRTOS vTaskDelay (non-blocking) & delay_us
│   └── pzem/                    # Giao tiếp cảm biến đo điện PZEM-004T
├── STM32F103C8Tx_FLASH.ld       # Linker script cho bộ nhớ 64K Flash, 20K RAM
├── Makefile                     # Build script đa luồng với arm-none-eabi-gcc
├── CMakeLists.txt               # Cấu hình CMake
├── run.sh                       # Script tự động hóa TUI Menu & CLI
├── stm32.sh                     # Symlink/wrapper sang run.sh
└── README.md                    # Tài liệu hướng dẫn này
```

---

## 3. Quy Trình Dòng Lệnh (CLI Workflow)

Yêu cầu đã cài đặt toolchain `arm-none-eabi-gcc` trên hệ thống Linux.

### Sử dụng Script tự động `run.sh` (hoặc `stm32.sh`):
```bash
cd /home/quanghaictu/DATN_NAM/firmware/POWER_QUALITY

# 1. Mở Menu tương tác điều khiển trực quan (Interactive Terminal UI):
./run.sh

# 2. Hoặc sử dụng các lệnh CLI trực tiếp:
./run.sh all [port] [baud] # Combo: Biên dịch + Nạp chip + Mở Serial Log ngay
./run.sh build             # Biên dịch firmware (tự động cập nhật compile_commands.json)
./run.sh rebuild           # Dọn dẹp và biên dịch lại từ đầu
./run.sh flash             # Nạp firmware xuống chip STM32 (ST-Link / OpenOCD)
./run.sh erase             # Xóa toàn bộ chip Flash (Mass Erase)
./run.sh size              # Phân tích chi tiết dung lượng Flash & SRAM đã sử dụng
./run.sh minicom [port]    # Mở Serial Monitor đọc log UART (mặc định: baud 115200)
./run.sh fix-usb           # Cấp quyền udev truy cập ST-Link & USB-UART trên Linux
./run.sh clean             # Xóa thư mục build/
```

### Hoặc sử dụng Makefile trực tiếp:
```bash
make clean
make -j$(nproc)
```

File nhị phân sau khi build nằm trong thư mục `build/`:
* `build/power_quality.elf` (Debug image)
* `build/power_quality.bin` (Binary image để nạp qua ST-Link hoặc UART Bootloader)
* `build/power_quality.hex` (Intel HEX format)

---

## 4. Chuẩn Lập Trình Cho AI & Lập Trình Viên

1. **Thư viện ngoại vi:** Sử dụng hàm và macro chuẩn từ `STM32F10x_StdPeriph_Driver` (ví dụ `GPIO_Init`, `RCC_APB2PeriphClockCmd`, `USART_Init`).
2. **Quản lý file:**
   - Khi thêm ngoại vi mới (Modbus, Sensor, ADC...), đặt file trong thư mục `Hardware/<module>/` gồm file `.c` và `.h`.
   - Cập nhật thêm include path và source file trong `Makefile` và `CMakeLists.txt`.
3. **Clock hệ thống:** Luôn đảm bảo `delay_init()` được gọi ở đầu `main()` trước khi sử dụng `delay_ms()` hoặc `delay_us()`.
