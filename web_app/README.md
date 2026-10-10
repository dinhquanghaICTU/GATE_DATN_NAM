# Web App & Backend API: IoT Gateway & HVAC Smart Control (DATN_NAM)

Hệ thống Web Quản Trị, Giám Sát và REST API điều khiển hai chiều cho trạm Gateway ESP32-S3 và Điều hòa Hunonic IR Pro. Phục vụ Đề tài Đồ Án Tốt Nghiệp tại **Trường Đại học Công nghệ Thông tin & Truyền thông - Đại học Thái Nguyên (ICTU)**.

---

## 1. So Sánh Bảng Phân Bổ Cổng (Chạy Chung Server Không Đụng Hàng)

Hệ thống được thiết kế độc lập hoàn toàn với `pump_backend` để có thể triển khai chung trên cùng 1 VPS/Server:

| Dịch Vụ | Dự án cũ `pump_backend` | Dự án ĐATN `web_app (DATN_NAM)` | Ghi Chú |
| :--- | :---: | :---: | :--- |
| **Giao diện Web Nginx** | Cổng `80` | **Cổng `8080`** | `http://<IP_SERVER>:8080` |
| **Backend FastAPI** | Cổng `8000` | **Cổng `8001`** | `http://<IP_SERVER>:8001/api/` |
| **Cơ sở dữ liệu PostgreSQL** | Cổng `5432` | **Cổng `5433`** | `localhost:5433` |
| **MQTT Broker** | HiveMQ Cloud (TLS 8883) | HiveMQ Cloud (TLS 8883) | Dùng chung Broker, khác Topic prefix |

---

## 2. Tài Liệu REST API Đầy Đủ (Interactive Swagger Docs)

Sau khi khởi chạy, tài liệu API trực quan chuẩn OpenAPI 3.0 sẽ tự động mở tại:
- **Swagger UI Interactive:** `http://localhost:8080/docs` (hoặc `http://<IP_SERVER>:8080/docs`)
- **ReDoc Documentation:** `http://localhost:8080/redoc`

### 2.1. Phân Hệ Xác Thực & Phân Quyền (`/api/auth`)

| Phương thức | Đường dẫn | Chức năng | Body / Header |
| :---: | :--- | :--- | :--- |
| `POST` | `/api/auth/login` | Đăng nhập cấp mã JWT Token | `{"username": "admin", "password": "..."}` |
| `POST` | `/api/auth/register` | Đăng ký người dùng mới | `{"username": "...", "password": "...", "fullname": "..."}` |
| `GET` | `/api/auth/me` | Lấy thông tin tài khoản hiện tại | Header: `Authorization: Bearer <token>` |
| `GET` | `/api/auth/users` | Xem danh sách người dùng (Admin) | Header: `Authorization: Bearer <token>` |

> **Tài khoản mặc định ban đầu:**  
> • Username: `admin`  
> • Password: `admin123`

---

### 2.2. Phân Hệ Điều Khiển Trạm Gateway ESP32-S3 (`/api/gateway`)

| Phương thức | Đường dẫn | Chức năng | Body / Header |
| :---: | :--- | :--- | :--- |
| `GET` | `/api/gateway/status` | Lấy trạng thái tức thời Gateway | Uptime, Free RAM, RSSI, Trạng thái Relay, Hunonic WS |
| `POST` | `/api/gateway/relay` | Đóng/ngắt Relay tải | `{"channel": 1, "state": 1}` *(ch: 1-2, state: 0-1)* |
| `POST` | `/api/gateway/reboot` | Khởi động lại Gateway từ xa | Gửi lệnh reboot vi điều khiển ESP32-S3 |
| `POST` | `/api/gateway/node_cmd` | Bắn lệnh xuống Node C3 | `{"node_id": 1, "cmd_id": 1, "param": 100}` qua ESP-NOW |
| `GET` | `/api/gateway/logs` | Lịch sử Heartbeat từ Database | Phân trang: `?limit=50` |

---

### 2.3. Phân Hệ Điều Khiển Điều Hòa HVAC (`/api/hvac`)

| Phương thức | Đường dẫn | Chức năng | Body / Header |
| :---: | :--- | :--- | :--- |
| `GET` | `/api/hvac/status` | Xem trạng thái Điều Hòa | Bật/Tắt, Nhiệt độ cài đặt, Chế độ, Tốc độ quạt |
| `POST` | `/api/hvac/control` | Phát lệnh điều khiển máy lạnh | `{"device_id": "3992007", "power": 1, "temp": 26, "mode": 1, "fan": 0}` |
| `POST` | `/api/hvac/sync` | Yêu cầu Gate đồng bộ thiết bị | Gửi lệnh `get_devices` để Gate tải lại thiết bị Hunonic |

*Giải thích thông số điều khiển:*
- `power`: `1` (Bật), `0` (Tắt).
- `temp`: `16` đến `30` (°C).
- `mode`: `0` (Auto), `1` (Cool - Làm lạnh), `2` (Heat - Sưởi), `3` (Dry - Hút ẩm), `4` (Fan - Quạt gió).
- `fan`: `0` (Auto), `1` (Thấp), `3` (Vừa), `5` (Mạnh).

---

### 2.4. Phân Hệ Cảm Biến Không Dây ESP-NOW (`/api/sensors`)

| Phương thức | Đường dẫn | Chức năng | Body / Header |
| :---: | :--- | :--- | :--- |
| `GET` | `/api/sensors/latest` | Dữ liệu cảm biến mới nhất | Nhiệt độ, Độ ẩm/Hiện diện, Điện áp pin, RSSI |
| `GET` | `/api/sensors/history` | Lịch sử đo đạc từ Database | Phân trang: `?node_id=1&limit=100` |

---

## 3. Hướng Dẫn Khởi Chạy (Deploy)

### Bước 1: Điều hướng vào thư mục
```bash
cd /home/quanghaictu/DATN_NAM/web_app
```

### Bước 2: Chạy script tự động 1 chạm
```bash
./deploy.sh
```

### Hoặc dùng lệnh Docker Compose tiêu chuẩn:
```bash
docker compose up -d --build
```

---

## 4. Các Liên Kết Truy Cập Hệ Thống

* **Trang Đăng Nhập:** `http://localhost:8080/login.html` (hoặc `http://<IP_SERVER>:8080`)
* **Bảng Điều Khiển Dashboard:** `http://localhost:8080/index.html`
* **Tài Liệu Swagger UI API:** `http://localhost:8080/docs`
* **Tài Liệu ReDoc API:** `http://localhost:8080/redoc`
