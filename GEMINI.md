# Firmware Guidelines (DATN_NAM)

Xem chi tiết quy định và cấu trúc đầy đủ tại file [AGENTS.md](file:///home/quanghaictu/DATN_NAM/AGENTS.md).

---

## 1. Gateway (ESP32-S3)
- **Target:** ESP32-S3 (Flash 16MB, PSRAM 8MB)
- **Framework:** ESP-IDF v5.3+
- **Environment:** `get_idf` (hoặc `. $HOME/esp/esp-idf/export.sh`)
- **Location:** `firmware/GATE`
- **Build & Flash:** `./esp.sh build` hoặc `idf.py build`

---

## 2. Power Quality Monitor (STM32F103C8T6)
- **Target:** STM32F103C8T6 (ARM Cortex-M3, 64KB Flash, 20KB SRAM)
- **Library:** STM32F10x Standard Peripheral Library (StdPeriph v3.5.0, Bare-metal Super Loop)
- **Toolchain:** `arm-none-eabi-gcc` + `make`
- **Location:** `firmware/POWER_QUALITY`
- **Build & Flash:** `./stm32.sh build` (hoặc `make -j$(nproc)`) | `./stm32.sh flash`
