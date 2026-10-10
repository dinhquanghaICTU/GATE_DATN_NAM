#!/bin/bash

# ==============================================================================
# Script Khởi Chạy Local (Không Cần Docker, Dùng SQLite + Python)
# Dành cho việc test nhanh trên máy tính cá nhân
# ==============================================================================

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR/backend"

echo "=========================================================="
echo "🚀 KHỞI CHẠY HỆ THỐNG WEB & API LOCAL (KHÔNG CẦN DOCKER)"
echo "=========================================================="

# 1. Kiểm tra môi trường Python
if ! command -v python3 &> /dev/null; then
    echo "❌ Lỗi: Chưa cài đặt Python3!"
    exit 1
fi

# 2. Tạo hoặc kích hoạt Virtual Environment (nếu có)
if [ ! -d "venv" ]; then
    echo "📦 Đang tạo môi trường ảo Python venv..."
    python3 -m venv venv || true
fi

if [ -f "venv/bin/activate" ]; then
    source venv/bin/activate
fi

# 3. Cài đặt các thư viện cần thiết
echo "📦 Đang kiểm tra thư viện..."
pip install --upgrade pip > /dev/null 2>&1 || true
pip install -r requirements.txt

# 4. Thiết lập biến môi trường chạy local (Dùng SQLite tự sinh file, không cần Postgres)
export DATABASE_URL="sqlite:///./gate_datn.db"
export PORT=8001
export JWT_SECRET="gate_datn_super_secret_jwt_key_ictu_2026"
export MQTT_BROKER_URI="mqtts://20476a36ce36478d90de6d5676587638.s1.eu.hivemq.cloud:8883"
export MQTT_USERNAME="quanghaictu"
export MQTT_PASSWORD="Zdinhquangha1234"
export DEFAULT_ADMIN_USER="admin"
export DEFAULT_ADMIN_PASS="admin123"

echo ""
echo "=========================================================="
echo "✅ HỆ THỐNG ĐÃ SẴN SÀNG! ĐANG CHẠY TẠI CÁC LIÊN KẾT SAU:"
echo "=========================================================="
echo "🌐 Trang Đăng Nhập       : http://localhost:8001/login.html"
echo "📊 Bảng Điều Khiển       : http://localhost:8001/index.html"
echo "📚 Tài Liệu API (Swagger): http://localhost:8001/docs"
echo "📖 Tài Liệu API (ReDoc)  : http://localhost:8001/redoc"
echo "----------------------------------------------------------"
echo "👤 Tài khoản đăng nhập   : admin / admin123"
echo "Bấm Ctrl + C để dừng."
echo "=========================================================="
echo ""

uvicorn app.main:app --host 0.0.0.0 --port 8001 --reload
