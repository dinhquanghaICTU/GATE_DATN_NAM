#!/bin/bash
# Script khởi chạy Web Dashboard Gateway & Điều hòa
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT=3000

echo "=========================================================="
echo "    KHỞI CHẠY WEB DASHBOARD ĐIỀU KHIỂN GATEWAY & HVAC"
echo "=========================================================="
echo "Địa chỉ truy cập: http://localhost:${PORT}"
echo "Hoặc mở trực tiếp file: ${DIR}/index.html trong trình duyệt"
echo "Bấm Ctrl + C để dừng."
echo "----------------------------------------------------------"

python3 -m http.server ${PORT} --directory "${DIR}"
