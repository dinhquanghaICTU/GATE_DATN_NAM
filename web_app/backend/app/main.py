import logging
from contextlib import asynccontextmanager
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import RedirectResponse

from app.config import settings
from app.database import init_db
from app.mqtt_service import start_mqtt_client, stop_mqtt_client
from app.routers import auth_router, gateway_router, hvac_router, sensor_router

# Thiết lập hệ thống ghi log
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] (%(name)s): %(message)s"
)
logger = logging.getLogger("MAIN_APP")

@asynccontextmanager
async def lifespan(app: FastAPI):
    # Khởi động ứng dụng
    logger.info("=" * 60)
    logger.info("  🚀 KHỞI ĐỘNG HỆ THỐNG BACKEND GATEWAY & HVAC (DATN NAM)  ")
    logger.info("=" * 60)
    init_db()
    start_mqtt_client()
    yield
    # Dọn dẹp khi tắt server
    logger.info("Đang dừng các dịch vụ nền...")
    stop_mqtt_client()
    logger.info("Hệ thống đã dừng an toàn.")

# Mô tả OpenAPI / Swagger UI chuyên nghiệp cho Đồ Án Tốt Nghiệp
API_DESCRIPTION = """
## HỆ THỐNG QUẢN TRỊ & ĐIỀU KHIỂN GATEWAY HVAC HAI CHIỀU (DATN_NAM)
Trường Đại học Công nghệ Thông tin & Truyền thông - Đại học Thái Nguyên (ICTU)

---

### Các Phân Hệ Chức Năng Cốt Lõi:
1. **Xác thực & Tài khoản (Authentication)**: Đăng nhập cấp mã JWT Token, đăng ký, phân quyền Admin/Operator.
2. **Điều khiển Trạm Gateway (ESP32-S3)**: Giám sát Uptime, RAM, RSSI, điều khiển Relay 2 kênh, Reboot từ xa.
3. **Điều khiển Điều Hòa (HVAC / Hunonic IR Pro)**: Bật/Tắt, Nhiệt độ, Chế độ làm mát/sưởi/hút ẩm, Quạt gió, đồng bộ trạng thái 2 chiều.
4. **Cảm biến Môi trường & Hiện diện**: Thu thập dữ liệu không dây ESP-NOW từ Node ESP32-C3 (Radar, Nhiệt - Ẩm SHT30).

---

### Hướng Dẫn Xác Thực Gọi API:
- Bước 1: Gọi `POST /api/auth/login` với tài khoản mặc định `admin` / `admin123`.
- Bước 2: Bấm nút **Authorize (Khóa xanh)** ở góc trên bên phải trang Swagger này, dán Access Token nhận được vào ô input.
- Bước 3: Thử nghiệm trực tiếp các endpoint điều khiển thiết bị!
"""

app = FastAPI(
    title=settings.PROJECT_NAME,
    description=API_DESCRIPTION,
    version=settings.VERSION,
    docs_url="/docs",
    redoc_url="/redoc",
    lifespan=lifespan
)

# Cấu hình CORS mở để frontend Nginx gọi API mượt mà
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Đăng ký các Router API
app.include_router(auth_router.router, prefix=settings.API_PREFIX)
app.include_router(gateway_router.router, prefix=settings.API_PREFIX)
app.include_router(hvac_router.router, prefix=settings.API_PREFIX)
app.include_router(sensor_router.router, prefix=settings.API_PREFIX)

@app.get("/health", tags=["5. Kiểm tra Hệ thống (System Health)"])
def health_check():
    """Kiểm tra tình trạng sống của Backend API"""
    return {
        "status": "healthy",
        "service": settings.PROJECT_NAME,
        "version": settings.VERSION,
        "port": settings.PORT
    }

# Mount giao diện Web tĩnh nếu có thư mục frontend/html (tiện lợi cho chạy local không cần Docker/Nginx)
import os
from fastapi.staticfiles import StaticFiles

frontend_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../frontend/html"))
if os.path.exists(frontend_dir):
    app.mount("/", StaticFiles(directory=frontend_dir, html=True), name="frontend")
else:
    @app.get("/", include_in_schema=False)
    def root():
        return RedirectResponse(url="/docs")
