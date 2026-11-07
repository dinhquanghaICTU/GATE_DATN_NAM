#!/usr/bin/env bash

# ==============================================================================
# Script Quản Lý, Biên Dịch và Nạp Firmware cho STM32F103C8T6 (POWER_QUALITY)
# Dành cho dự án DATN_NAM - Chuẩn thư viện Standard Peripheral Library (StdPeriph v3.5.0)
# ==============================================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_DIR"

TARGET="power_quality"
ELF_FILE="build/${TARGET}.elf"
BIN_FILE="build/${TARGET}.bin"
HEX_FILE="build/${TARGET}.hex"
DEFAULT_BAUD=115200

# ------------------------------------------------------------------------------
# Hàm tìm kiếm cổng Serial tự động
# ------------------------------------------------------------------------------
find_serial_port() {
    local ports=( $(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true) )
    if [ ${#ports[@]} -gt 0 ]; then
        echo "${ports[0]}"
    else
        echo ""
    fi
}

# ------------------------------------------------------------------------------
# Hàm tự động sinh compile_commands.json cho VS Code C/C++ IntelliSense
# ------------------------------------------------------------------------------
update_compile_commands() {
    python3 -c "
import json, subprocess, os, shlex
cwd = os.getcwd()
res = subprocess.run(['make', '-n', '-B'], capture_output=True, text=True)
entries = []
for line in res.stdout.splitlines():
    line = line.strip()
    if 'arm-none-eabi-gcc' in line and ' -c ' in line:
        parts = shlex.split(line)
        c_files = [p for p in parts if p.endswith('.c') or p.endswith('.s')]
        if c_files:
            entries.append({
                'directory': cwd,
                'command': line,
                'file': os.path.abspath(c_files[0])
            })
if entries:
    with open('compile_commands.json', 'w') as f:
        json.dump(entries, f, indent=2)
" 2>/dev/null || true
}

# ------------------------------------------------------------------------------
# Hàm Phân Tích Dung Lượng Bộ Nhớ (Flash & RAM)
# ------------------------------------------------------------------------------
show_memory_usage() {
    if [ ! -f "$ELF_FILE" ]; then
        echo -e "${RED}❌ Chưa có file $ELF_FILE. Hãy biên dịch trước!${NC}"
        return 1
    fi

    echo -e "${CYAN}${BOLD}📊 [BỘ NHỚ] Phân tích dung lượng STM32F103C8T6:${NC}"
    arm-none-eabi-size "$ELF_FILE"
    echo ""

    read text data bss dec hex filename < <(arm-none-eabi-size "$ELF_FILE" | tail -n 1)
    
    flash_used=$((text + data))
    ram_used=$((data + bss))
    flash_total=65536  # 64 KB Flash
    ram_total=20480    # 20 KB SRAM

    flash_pct=$(awk "BEGIN {printf \"%.2f\", ($flash_used / $flash_total) * 100}")
    ram_pct=$(awk "BEGIN {printf \"%.2f\", ($ram_used / $ram_total) * 100}")

    echo -e "  🔹 ${BOLD}Flash ROM:${NC} $flash_used / $flash_total bytes (${CYAN}${flash_pct}%${NC})"
    echo -e "  🔹 ${BOLD}SRAM:     ${NC} $ram_used / $ram_total bytes (${CYAN}${ram_pct}%${NC})"
    echo ""
}

# ------------------------------------------------------------------------------
# 1. Hàm Xóa Toàn Bộ Chip Flash (Erase)
# ------------------------------------------------------------------------------
do_erase() {
    echo -e "${YELLOW}${BOLD}>>> ĐANG XÓA TOÀN BỘ FLASH TRÊN STM32F103 (Mass Erase)...${NC}"
    local ret=0

    if which st-flash > /dev/null 2>&1; then
        st-flash erase || ret=$?
    else
        openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init; reset halt; stm32x mass_erase 0; reset run; exit" || ret=$?
    fi

    if [ $ret -ne 0 ]; then
        echo -e "${YELLOW}[CẢNH BÁO] Thử lại với sudo...${NC}"
        if which st-flash > /dev/null 2>&1; then
            sudo st-flash erase || return 1
        else
            sudo openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "init; reset halt; stm32x mass_erase 0; reset run; exit" || return 1
        fi
    fi
    echo -e "${GREEN}${BOLD}✓ Xóa Flash thành công!${NC}"
    return 0
}

# ------------------------------------------------------------------------------
# 2. Hàm Biên Dịch (Build)
# ------------------------------------------------------------------------------
do_build() {
    echo -e "${CYAN}${BOLD}>>> ĐANG BIÊN DỊCH FIRMWARE (make)...${NC}"
    if ! make -j"$(nproc)"; then
        echo -e "${RED}${BOLD}[LỖI] Biên dịch thất bại! Vui lòng kiểm tra lại code.${NC}"
        return 1
    fi
    update_compile_commands
    echo -e "${GREEN}${BOLD}✓ Biên dịch thành công! File bin: $BIN_FILE${NC}"
    show_memory_usage
    return 0
}

# ------------------------------------------------------------------------------
# 3. Hàm Dọn Dẹp và Biên Dịch Lại (Rebuild)
# ------------------------------------------------------------------------------
do_rebuild() {
    echo -e "${YELLOW}${BOLD}>>> DỌN DẸP VÀ BIÊN DỊCH LẠI TỪ ĐẦU...${NC}"
    make clean
    do_build
}

# ------------------------------------------------------------------------------
# 4. Hàm Nạp Firmware (Flash)
# ------------------------------------------------------------------------------
do_flash() {
    if [ ! -f "$BIN_FILE" ]; then
        echo -e "${YELLOW}Chưa thấy file build, tiến hành biên dịch trước...${NC}"
        if ! do_build; then
            return 1
        fi
    fi

    # Kiểm tra mạch ST-Link có cắm trên cổng USB không
    if ! lsusb | grep -qi "0483:"; then
        echo -e "${RED}${BOLD}=====================================================${NC}"
        echo -e "${RED}${BOLD} [LỖI] KHÔNG TÌM THẤY MẠCH NẠP ST-LINK TRÊN CỔNG USB!${NC}"
        echo -e "${YELLOW} Hãy cắm mạch ST-Link vào máy tính trước khi nạp!${NC}"
        echo -e "${RED}${BOLD}=====================================================${NC}"
        return 1
    fi

    echo -e "${CYAN}${BOLD}>>> ĐANG NẠP FIRMWARE XUỐNG CHIP (ST-Link)...${NC}"
    
    local flashed=0

    # Ưu tiên 1: st-flash
    if which st-flash > /dev/null 2>&1; then
        if st-flash --reset write "$BIN_FILE" 0x08000000; then
            flashed=1
        else
            echo -e "${YELLOW}[CẢNH BÁO] Quyền USB bị hạn chế, thử lại với sudo st-flash...${NC}"
            if sudo st-flash --reset write "$BIN_FILE" 0x08000000; then
                flashed=1
            fi
        fi
    fi

    # Fallback 2: OpenOCD nếu st-flash không thành công
    if [ $flashed -eq 0 ] && which openocd > /dev/null 2>&1; then
        set -o pipefail
        local ret=0
        openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "program $BIN_FILE 0x08000000 verify reset exit" 2>&1 | tee /tmp/openocd_flash.log || ret=$?
        set +o pipefail

        if [ $ret -eq 0 ]; then
            flashed=1
        elif grep -q "LIBUSB_ERROR_ACCESS" /tmp/openocd_flash.log 2>/dev/null; then
            echo -e "${YELLOW}${BOLD}[PHÁT HIỆN LỖI QUYỀN USB] ST-Link bị chặn bởi Linux permissions.${NC}"
            echo -e "${CYAN}>>> Đang tự động thử lại bằng 'sudo openocd'...${NC}"
            if sudo openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "program $BIN_FILE 0x08000000 verify reset exit"; then
                flashed=1
            fi
        fi
    fi

    if [ $flashed -ne 1 ]; then
        echo -e "${RED}${BOLD}=========================================================${NC}"
        echo -e "${RED}${BOLD} [LỖI] NẠP FIRMWARE THẤT BẠI! CODE MỚI CHƯA ĐƯỢC GHI VÀO CHIP!${NC}"
        echo -e "${YELLOW} Các nguyên nhân phổ biến:${NC}"
        echo -e "  1. Mạch ST-Link bị lỏng cổng USB."
        echo -e "  2. 4 dây SWD (SWDIO, SWCLK, GND, 3.3V) cắm vào STM32 bị lỏng hoặc chập."
        echo -e "  3. STM32 bị kẹt chế độ Sleep / Read-out Protection (Hãy thử chạy option Erase).${NC}"
        echo -e "${RED}${BOLD}=========================================================${NC}"
        return 1
    fi

    echo -e "${GREEN}${BOLD}✓ NẠP THÀNH CÔNG! CODE MỚI ĐÃ ĐƯỢC GHI VÀO CHIP STM32.${NC}"
    return 0
}

# ------------------------------------------------------------------------------
# 5. Hàm Mở Serial Log Monitor (Minicom / Python)
# ------------------------------------------------------------------------------
do_minicom() {
    local port="$1"
    local baud="${2:-$DEFAULT_BAUD}"

    if [ -z "$port" ]; then
        port=$(find_serial_port)
    fi

    if [ -z "$port" ] || [ ! -e "$port" ]; then
        echo -e "${RED}[CẢNH BÁO] Không tìm thấy cổng Serial (/dev/ttyUSB* hoặc /dev/ttyACM*).${NC}"
        echo -e "Danh sách cổng hiện có:"
        ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || echo "  (Không có cổng USB Serial nào được kết nối)"
        read -rp "Nhập đường dẫn cổng UART thủ công (ví dụ /dev/ttyUSB0): " input_port
        port="$input_port"
    fi

    if [ -z "$port" ] || [ ! -e "$port" ]; then
        echo -e "${RED}[LỖI] Cổng '$port' không tồn tại! Hãy cắm dây USB to TTL (CH340/CP2102) vào máy.${NC}"
        return 1
    fi

    # Đảm bảo quyền đọc ghi cổng Serial
    if [ ! -r "$port" ] || [ ! -w "$port" ]; then
        echo -e "${YELLOW}Cấp quyền đọc ghi cho $port...${NC}"
        sudo chmod 666 "$port" 2>/dev/null || true
    fi

    echo -e "${CYAN}${BOLD}=====================================================${NC}"
    echo -e "${CYAN}${BOLD}           MỞ SERIAL LOG MONITOR                     ${NC}"
    echo -e " Cổng: ${GREEN}${BOLD}$port${NC} | Baudrate: ${GREEN}${BOLD}$baud${NC}"
    echo -e " ${YELLOW}Mẹo thoát Minicom:${NC} Bấm ${BOLD}Ctrl + A${NC} rồi bấm ${BOLD}X${NC} -> chọn ${BOLD}Yes${NC}"
    echo -e "${CYAN}${BOLD}=====================================================${NC}"
    sleep 1

    if which minicom > /dev/null 2>&1; then
        minicom -D "$port" -b "$baud" -o -w -c on
    elif python3 -m serial.tools.miniterm --help > /dev/null 2>&1; then
        python3 -m serial.tools.miniterm "$port" "$baud"
    else
        echo -e "${RED}❌ Hãy cài minicom hoặc pyserial: sudo apt install minicom${NC}"
    fi
}

# ------------------------------------------------------------------------------
# 6. Chế độ kết hợp: Build + Flash + Mở Serial Log
# ------------------------------------------------------------------------------
do_all() {
    local port="$1"
    local baud="${2:-$DEFAULT_BAUD}"

    if do_build; then
        if do_flash; then
            echo -e "${GREEN}${BOLD}>>> Nạp hoàn tất! Đang mở Serial Monitor xem log...${NC}"
            sleep 1
            do_minicom "$port" "$baud"
        else
            echo -e "${RED}Nạp code thất bại. Bạn có muốn mở Serial Monitor không? [y/N]: ${NC}"
            read -r yn
            if [[ "$yn" =~ ^[yY]$ ]]; then
                do_minicom "$port" "$baud"
            fi
        fi
    fi
}

# ------------------------------------------------------------------------------
# 7. Menu Tương Tác
# ------------------------------------------------------------------------------
show_menu() {
    while true; do
        clear
        echo -e "${CYAN}${BOLD}========================================================${NC}"
        echo -e "${CYAN}${BOLD}     STM32F103C8T6 TOOL CONTROLLER (POWER_QUALITY)      ${NC}"
        echo -e "${CYAN}${BOLD}========================================================${NC}"
        local detected_port
        detected_port=$(find_serial_port)
        if [ -n "$detected_port" ]; then
            echo -e " Cổng Serial tự nhận diện: ${GREEN}${BOLD}$detected_port${NC}"
        else
            echo -e " Cổng Serial tự nhận diện: ${RED}Chưa cắm USB-UART (CH340/CP2102)${NC}"
        fi
        echo -e " Baudrate mặc định:       ${GREEN}${BOLD}$DEFAULT_BAUD${NC}"
        echo -e "${CYAN}--------------------------------------------------------${NC}"
        echo -e "  ${BOLD}1)${NC} ${GREEN}Build${NC}             - Biên dịch firmware (make)"
        echo -e "  ${BOLD}2)${NC} ${YELLOW}Rebuild${NC}           - Xóa bản cũ và biên dịch lại sạch sẽ"
        echo -e "  ${BOLD}3)${NC} ${CYAN}Flash${NC}             - Nạp firmware xuống chip (ST-Link)"
        echo -e "  ${BOLD}4)${NC} ${GREEN}Build + Flash${NC}     - Biên dịch và nạp luôn"
        echo -e "  ${BOLD}5)${NC} ${RED}Erase Chip${NC}        - Xóa toàn bộ bộ nhớ Flash của chip"
        echo -e "  ${BOLD}6)${NC} ${CYAN}Open Serial/Minicom${NC}- Mở Serial Monitor xem log UART"
        echo -e "  ${BOLD}7)${NC} ${BOLD}Build+Flash+Log${NC}   - Combo: Build + Nạp + Mở Serial ngay"
        echo -e "  ${BOLD}8)${NC} Phân tích bộ nhớ   - Xem % dung lượng Flash ROM & SRAM"
        echo -e "  ${BOLD}9)${NC} Fix USB Permission - Cấp quyền vĩnh viễn cho ST-Link/UART"
        echo -e "  ${BOLD}10)${NC} Clean            - Xóa thư mục build"
        echo -e "  ${BOLD}0)${NC} Thoát"
        echo -e "${CYAN}========================================================${NC}"
        read -rp "Chọn chức năng [0-10]: " choice

        case "$choice" in
            1)
                do_build
                read -rp "Nhấn [Enter] để tiếp tục..."
                ;;
            2)
                do_rebuild
                read -rp "Nhấn [Enter] để tiếp tục..."
                ;;
            3)
                do_flash
                read -rp "Nhấn [Enter] để tiếp tục..."
                ;;
            4)
                if do_build; then
                    do_flash
                fi
                read -rp "Nhấn [Enter] để tiếp tục..."
                ;;
            5)
                do_erase
                read -rp "Nhấn [Enter] để tiếp tục..."
                ;;
            6)
                do_minicom
                read -rp "Nhấn [Enter] để tiếp tục..."
                ;;
            7)
                do_all
                read -rp "Nhấn [Enter] để tiếp tục..."
                ;;
            8)
                show_memory_usage
                read -rp "Nhấn [Enter] để tiếp tục..."
                ;;
            9)
                ./fix_usb_permission.sh
                read -rp "Nhấn [Enter] để tiếp tục..."
                ;;
            10)
                make clean
                echo -e "${GREEN}Đã xóa thư mục build!${NC}"
                read -rp "Nhấn [Enter] để tiếp tục..."
                ;;
            0|q|Q)
                echo "Tạm biệt!"
                exit 0
                ;;
            *)
                echo -e "${RED}Lựa chọn không hợp lệ!${NC}"
                sleep 1
                ;;
        esac
    done
}

# ==============================================================================
# Xử lý tham số dòng lệnh CLI
# ==============================================================================
case "$1" in
    build)
        do_build
        ;;
    rebuild)
        do_rebuild
        ;;
    clean)
        make clean
        ;;
    flash)
        do_flash
        ;;
    erase)
        do_erase
        ;;
    size)
        show_memory_usage
        ;;
    minicom|monitor|serial)
        do_minicom "$2" "$3"
        ;;
    all|bfm)
        do_all "$2" "$3"
        ;;
    fix|fix-usb)
        ./fix_usb_permission.sh
        ;;
    help|--help|-h)
        echo -e "${BOLD}Cách sử dụng:${NC}"
        echo -e "  ${GREEN}./run.sh${NC}                           : Mở menu tương tác đầy đủ"
        echo -e "  ${GREEN}./run.sh all [cổng] [baud]${NC}         : Combo: Build + Flash + Mở Serial Log ngay"
        echo -e "  ${GREEN}./run.sh build${NC}                     : Biên dịch dự án"
        echo -e "  ${GREEN}./run.sh rebuild${NC}                   : Dọn dẹp và biên dịch lại từ đầu"
        echo -e "  ${GREEN}./run.sh flash${NC}                     : Nạp firmware xuống chip STM32"
        echo -e "  ${GREEN}./run.sh erase${NC}                     : Xóa sạch bộ nhớ Flash STM32"
        echo -e "  ${GREEN}./run.sh size${NC}                      : Xem phân tích dung lượng Flash & RAM"
        echo -e "  ${GREEN}./run.sh minicom [cổng] [baud]${NC}     : Mở Serial Monitor xem log (mặc định 115200)"
        echo -e "  ${GREEN}./run.sh fix-usb${NC}                   : Cấp quyền udev cho ST-Link & USB-UART"
        echo -e "  ${GREEN}./run.sh clean${NC}                     : Xóa thư mục build"
        ;;
    *)
        if [ -n "$1" ]; then
            echo -e "${RED}[LỖI] Lựa chọn '$1' không tồn tại. Gõ ./run.sh help để xem hướng dẫn.${NC}"
            exit 1
        fi
        show_menu
        ;;
esac
