#!/bin/bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "=========================================================="
echo "🚀 1. KIỂM TRA MÔI TRƯỜNG DOCKER..."
echo "=========================================================="
if ! command -v docker &> /dev/null; then
    echo "❌ Lỗi: Máy chủ chưa cài đặt Docker!"
    exit 1
fi

echo "=========================================================="
echo "🔨 2. BIÊN DỊCH CÁC CONTAINER (BACKEND + FRONTEND)..."
echo "=========================================================="
docker compose build

echo "=========================================================="
echo "🔄 3. KHỞI CHẠY HỆ THỐNG TRÊN CÁC CỔNG MỚI..."
echo "=========================================================="
docker compose up -d

echo ""
echo "=========================================================="
echo "✅ TRIỂN KHAI HOÀN TẤT THÀNH CÔNG!"
echo "=========================================================="
echo "🌐 Giao diện Web Đăng Nhập : http://localhost:8080"
echo "📊 Bảng Điều Khiển Dashboard: http://localhost:8080/index.html"
echo "📚 Tài Liệu API Swagger UI : http://localhost:8080/docs"
echo "📖 Tài Liệu API ReDoc      : http://localhost:8080/redoc"
echo "🔌 Cổng Backend API Trực tiếp: http://localhost:8001/api/"
echo "🗄️ Cổng PostgreSQL Riêng   : localhost:5433"
echo "----------------------------------------------------------"
echo "👤 Tài khoản đăng nhập mặc định: admin / admin123"
echo "=========================================================="
echo ""
echo "Đang theo dõi log của Backend API (Bấm Ctrl+C để thoát log):"
docker compose logs -f --tail=30 backend_api
