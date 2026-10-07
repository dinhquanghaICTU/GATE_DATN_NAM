#!/usr/bin/env bash

# ==============================================================================
# ESP32-S3 Build & Flash Automation Script (GATE FIRMWARE - DATN_NAM)
# ==============================================================================

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CURRENT_TARGET="esp32s3"
DEVICE_NAME="🌐 GATEWAY MASTER (ESP32-S3)"
PORT_DEVICE=""

# Color codes
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
NC='\033[0m' # No Color

ensure_idf_env() {
    if ! command -v idf.py &> /dev/null; then
        echo -e "${YELLOW}[INFO] Đang nạp môi trường ESP-IDF...${NC}"
        if [ -f "$HOME/esp/esp-idf/export.sh" ]; then
            . "$HOME/esp/esp-idf/export.sh" > /dev/null 2>&1
        else
            echo -e "${RED}[ERROR] Không tìm thấy ~/esp/esp-idf/export.sh! Hãy kiểm tra lại đường dẫn SDK.${NC}"
            exit 1
        fi
    fi
}

get_all_ports() {
    ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
}

auto_assign_port() {
    local PORTS=($(get_all_ports))
    if [ ${#PORTS[@]} -ge 1 ]; then
        if [ -z "$PORT_DEVICE" ]; then
            # Ưu tiên ACM0 cho S3 (USB Serial/JTAG), sau đó USB0
            for p in "${PORTS[@]}"; do
                if [[ "$p" == *"ACM"* ]]; then
                    PORT_DEVICE="$p"
                    break
                fi
            done
            [ -z "$PORT_DEVICE" ] && PORT_DEVICE="${PORTS[0]}"
        fi
    fi
}

check_port_validity() {
    if [ -n "$PORT_DEVICE" ] && [ ! -e "$PORT_DEVICE" ]; then
        PORT_DEVICE=""
    fi
    auto_assign_port
}

select_port_menu() {
    local PORTS=($(get_all_ports))
    echo -e "${BLUE}Danh sách các cổng Serial đang cắm:${NC}"
    if [ ${#PORTS[@]} -eq 0 ]; then
        echo -e "${RED}  (Không tìm thấy thiết bị nào qua cổng /dev/ttyUSB* hoặc /dev/ttyACM*)${NC}"
        PORT_DEVICE=""
        return
    fi

    for i in "${!PORTS[@]}"; do
        echo -e "  [$((i+1))] ${PORTS[$i]}"
    done
    echo -n "Chọn số thứ tự cổng: "
    read -r choice
    if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "${#PORTS[@]}" ]; then
        PORT_DEVICE="${PORTS[$((choice-1))]}"
        echo -e "${GREEN}Đã gán cổng: $PORT_DEVICE${NC}"
    else
        echo -e "${RED}Lựa chọn không hợp lệ!${NC}"
    fi
}

build_project() {
    ensure_idf_env
    cd "$PROJECT_DIR" || exit 1
    echo -e "${BLUE}======================================================${NC}"
    echo -e "${BOLD}${CYAN}🔨 [BUILD] Đang biên dịch dự án Gateway...${NC}"
    echo -e "${BLUE}======================================================${NC}"
    idf.py build
}

flash_project() {
    ensure_idf_env
    check_port_validity
    if [ -z "$PORT_DEVICE" ]; then
        echo -e "${RED}[ERROR] Không tìm thấy cổng nạp nào! Hãy cắm dây kết nối ESP32-S3.${NC}"
        select_port_menu
        [ -z "$PORT_DEVICE" ] && return 1
    fi

    cd "$PROJECT_DIR" || exit 1
    echo -e "${BLUE}======================================================${NC}"
    echo -e "${BOLD}${YELLOW}⚡ [FLASH] Đang nạp firmware vào cổng: $PORT_DEVICE...${NC}"
    echo -e "${BLUE}======================================================${NC}"
    idf.py -p "$PORT_DEVICE" flash
}

monitor_project() {
    ensure_idf_env
    check_port_validity
    if [ -z "$PORT_DEVICE" ]; then
        echo -e "${RED}[ERROR] Không tìm thấy cổng COM nào để mở Monitor!${NC}"
        select_port_menu
        [ -z "$PORT_DEVICE" ] && return 1
    fi

    cd "$PROJECT_DIR" || exit 1
    echo -e "${BLUE}======================================================${NC}"
    echo -e "${BOLD}${MAGENTA}📺 [MONITOR] Mở Serial Monitor trên: $PORT_DEVICE (Bấm Ctrl+] để thoát)${NC}"
    echo -e "${BLUE}======================================================${NC}"
    idf.py -p "$PORT_DEVICE" monitor
}

flash_and_monitor() {
    flash_project && monitor_project
}

clean_project() {
    ensure_idf_env
    cd "$PROJECT_DIR" || exit 1
    echo -e "${YELLOW}🧹 Đang dọn dẹp thư mục build...${NC}"
    idf.py fullclean
    echo -e "${GREEN}Dọn dẹp hoàn tất!${NC}"
}

menuconfig_project() {
    ensure_idf_env
    cd "$PROJECT_DIR" || exit 1
    idf.py menuconfig
}

set_target_s3() {
    ensure_idf_env
    cd "$PROJECT_DIR" || exit 1
    echo -e "${CYAN}🎯 Đặt target cho ESP32-S3...${NC}"
    idf.py set-target esp32s3
}

erase_flash() {
    ensure_idf_env
    check_port_validity
    if [ -z "$PORT_DEVICE" ]; then
        select_port_menu
        [ -z "$PORT_DEVICE" ] && return 1
    fi
    echo -e "${RED}⚠️  CẢNH BÁO: Đang xóa sạch toàn bộ Flash trên $PORT_DEVICE...${NC}"
    idf.py -p "$PORT_DEVICE" erase-flash
}

# Xử lý tham số truyền vào từ CLI (nếu có)
if [ "$1" == "build" ]; then
    build_project; exit $?
elif [ "$1" == "flash" ]; then
    flash_project; exit $?
elif [ "$1" == "monitor" ]; then
    monitor_project; exit $?
elif [ "$1" == "flash-monitor" ] || [ "$1" == "fm" ]; then
    flash_and_monitor; exit $?
elif [ "$1" == "clean" ]; then
    clean_project; exit $?
elif [ "$1" == "menuconfig" ]; then
    menuconfig_project; exit $?
elif [ "$1" == "target" ]; then
    set_target_s3; exit $?
elif [ "$1" == "erase" ]; then
    erase_flash; exit $?
fi

# Giao diện Interactive Menu
while true; do
    check_port_validity
    clear
    echo -e "${CYAN}================================================================${NC}"
    echo -e "${BOLD}${MAGENTA}       ESP32-S3 GATEWAY FIRMWARE TOOLKIT (DATN_NAM)       ${NC}"
    echo -e "${CYAN}================================================================${NC}"
    echo -e "Thiết bị hiện tại: ${BOLD}${GREEN}$DEVICE_NAME${NC}"
    echo -e "Chip Target     : ${BOLD}${YELLOW}$CURRENT_TARGET${NC}"
    echo -e "Cổng nạp        : ${BOLD}${CYAN}${PORT_DEVICE:-"(Chưa chọn/chưa cắm)"}${NC}"
    echo -e "Đường dẫn       : ${BLUE}$PROJECT_DIR${NC}"
    echo -e "${CYAN}----------------------------------------------------------------${NC}"
    echo -e " [1] 🔨 Build Firmware (idf.py build)"
    echo -e " [2] ⚡ Flash Firmware (idf.py flash)"
    echo -e " [3] 📺 Monitor Serial (idf.py monitor)"
    echo -e " [4] 🚀 Flash & Monitor (Nạp và mở log ngay)"
    echo -e " [5] 🔌 Đổi / Chọn cổng Serial"
    echo -e " [6] 🧹 Clean Build (Xóa cache build)"
    echo -e " [7] ⚙️  Menuconfig (Cấu hình ESP-IDF)"
    echo -e " [8] 🎯 Set Target ESP32-S3"
    echo -e " [9] 🗑️  Erase Flash (Xóa sạch bộ nhớ chip)"
    echo -e " [0] 🚪 Thoát"
    echo -e "${CYAN}================================================================${NC}"
    echo -n "Nhập lựa chọn của bạn [0-9]: "
    read -r opt

    case $opt in
        1) build_project; read -p "Bấm Enter để tiếp tục..." ;;
        2) flash_project; read -p "Bấm Enter để tiếp tục..." ;;
        3) monitor_project; read -p "Bấm Enter để tiếp tục..." ;;
        4) flash_and_monitor; read -p "Bấm Enter để tiếp tục..." ;;
        5) select_port_menu; read -p "Bấm Enter để tiếp tục..." ;;
        6) clean_project; read -p "Bấm Enter để tiếp tục..." ;;
        7) menuconfig_project; read -p "Bấm Enter để tiếp tục..." ;;
        8) set_target_s3; read -p "Bấm Enter để tiếp tục..." ;;
        9) erase_flash; read -p "Bấm Enter để tiếp tục..." ;;
        0) echo -e "${GREEN}Tạm biệt!${NC}"; exit 0 ;;
        *) echo -e "${RED}Lựa chọn không hợp lệ!${NC}"; sleep 1 ;;
    esac
done
