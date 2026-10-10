# Sử dụng Python 3.11 slim nhẹ, ổn định và tối ưu
FROM python:3.11-slim

WORKDIR /app

# Cài đặt công cụ build cần thiết cho psycopg2
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc \
    libpq-dev \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Cài đặt thư viện Python
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy mã nguồn backend
COPY app ./app

# Mở cổng 8001 (khác với 8000 của pump_backend)
EXPOSE 8001

# Chạy server FastAPI qua Uvicorn trên cổng 8001
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8001"]
