from pydantic_settings import BaseSettings
from typing import Optional

class Settings(BaseSettings):
    PROJECT_NAME: str = "Smart HVAC & Gateway Control Center API"
    VERSION: str = "1.0.0"
    API_PREFIX: str = "/api"
    
    # Cấu hình Cổng
    PORT: int = 8001
    
    # Database PostgreSQL
    DATABASE_URL: str = "postgresql+psycopg2://gate_admin:GatePass2026@gate_postgres:5432/gate_datn_db"
    
    # Bảo mật JWT
    JWT_SECRET: str = "gate_datn_super_secret_jwt_key_ictu_2026"
    JWT_ALGORITHM: str = "HS256"
    ACCESS_TOKEN_EXPIRE_MINUTES: int = 60 * 24 * 7  # 7 ngày
    
    # Cấu hình HiveMQ Cloud (Dùng chung broker với ESP32-S3)
    MQTT_BROKER_URI: str = "mqtts://20476a36ce36478d90de6d5676587638.s1.eu.hivemq.cloud:8883"
    MQTT_USERNAME: str = "quanghaictu"
    MQTT_PASSWORD: str = "Zdinhquangha1234"
    
    # Topics MQTT của Gateway
    TOPIC_GATE_STATUS: str = "gate/status"
    TOPIC_GATE_TELEMETRY: str = "gate/telemetry"
    TOPIC_GATE_COMMAND: str = "gate/command"
    TOPIC_GATE_OTA: str = "gate/ota"
    
    # Tài khoản Admin mặc định khởi tạo
    DEFAULT_ADMIN_USER: str = "admin"
    DEFAULT_ADMIN_PASS: str = "admin123"
    DEFAULT_ADMIN_FULLNAME: str = "Quản Trị Viên ICTU"

    class Config:
        env_file = ".env"
        extra = "allow"

settings = Settings()
