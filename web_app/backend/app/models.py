from typing import Optional
from datetime import datetime, timezone
from sqlmodel import SQLModel, Field
from pydantic import BaseModel

def get_utc_now() -> datetime:
    return datetime.now(timezone.utc)

# ==============================================================================
# 1. DATABASE MODELS (BẢNG CƠ SỞ DỮ LIỆU)
# ==============================================================================

class User(SQLModel, table=True):
    __tablename__ = "users"
    
    id: Optional[int] = Field(default=None, primary_key=True)
    username: str = Field(unique=True, index=True, max_length=50)
    password_hash: str = Field(max_length=255)
    fullname: str = Field(default="", max_length=100)
    role: str = Field(default="admin", max_length=20)  # admin / operator / viewer
    is_active: bool = Field(default=True)
    created_at: datetime = Field(default_factory=get_utc_now)


class GatewayStatusLog(SQLModel, table=True):
    __tablename__ = "gateway_status_logs"
    
    id: Optional[int] = Field(default=None, primary_key=True)
    device: str = Field(default="esp32s3_gate", max_length=50)
    uptime_sec: int = Field(default=0)
    free_heap: int = Field(default=0)
    rssi: int = Field(default=-100)
    relay1: int = Field(default=0)
    relay2: int = Field(default=0)
    ac_id: str = Field(default="", max_length=32)
    ac_name: str = Field(default="", max_length=64)
    ac_power: int = Field(default=0)
    ac_temp: int = Field(default=25)
    ws_connected: int = Field(default=0)
    total_node_pkts: int = Field(default=0)
    fw_ver: str = Field(default="1.0.0", max_length=32)
    created_at: datetime = Field(default_factory=get_utc_now, index=True)


class SensorRecord(SQLModel, table=True):
    __tablename__ = "sensor_records"
    
    id: Optional[int] = Field(default=None, primary_key=True)
    node_id: int = Field(default=1, index=True)
    packet_id: int = Field(default=0)
    mac: str = Field(default="", max_length=30)
    val1: float = Field(default=0.0)         # Nhiệt độ hoặc trạng thái Hiện diện
    val2: float = Field(default=0.0)         # Độ ẩm
    battery: float = Field(default=0.0)      # Điện áp pin (V)
    fw: str = Field(default="1.0.0", max_length=32)
    rssi: int = Field(default=-100)
    created_at: datetime = Field(default_factory=get_utc_now, index=True)


class ControlAudit(SQLModel, table=True):
    __tablename__ = "control_audits"
    
    id: Optional[int] = Field(default=None, primary_key=True)
    username: str = Field(default="system", max_length=50)
    target: str = Field(default="gateway", max_length=50)     # relay / hvac / node / gate
    command: str = Field(max_length=50)                       # relay_on / ac_set / reboot ...
    payload: str = Field(default="")                          # Chi tiết JSON lệnh
    status: str = Field(default="sent", max_length=20)        # sent / error
    created_at: datetime = Field(default_factory=get_utc_now, index=True)


# ==============================================================================
# 2. REQUEST & RESPONSE SCHEMAS (API DTO)
# ==============================================================================

class UserLoginRequest(BaseModel):
    username: str
    password: str

class UserRegisterRequest(BaseModel):
    username: str
    password: str
    fullname: Optional[str] = ""
    role: Optional[str] = "operator"

class TokenResponse(BaseModel):
    access_token: str
    token_type: str = "bearer"
    user: dict

class UserResponse(BaseModel):
    id: int
    username: str
    fullname: str
    role: str
    is_active: bool
    created_at: datetime

class RelayControlRequest(BaseModel):
    channel: int = Field(default=1, ge=1, le=2, description="Kênh Relay: 1 hoặc 2")
    state: int = Field(default=1, ge=0, le=1, description="Trạng thái: 1 = Bật, 0 = Tắt")

class HVACControlRequest(BaseModel):
    device_id: Optional[str] = Field(default="", description="ID điều hòa Hunonic (để trống sẽ dùng thiết bị mặc định)")
    power: int = Field(default=1, ge=0, le=1, description="1 = Bật, 0 = Tắt")
    temp: int = Field(default=25, ge=16, le=35, description="Nhiệt độ cài đặt (16 - 35 °C)")
    mode: int = Field(default=1, ge=0, le=5, description="0: Auto, 1: Cool, 2: Heat, 3: Dry, 4: Fan")
    fan: int = Field(default=0, ge=0, le=5, description="0: Auto, 1: Min, 3: Med, 5: Max")

class NodeCommandRequest(BaseModel):
    node_id: int = Field(default=1, description="ID Node nhận lệnh (ESP32-C3)")
    cmd_id: int = Field(default=1, description="Mã lệnh")
    param: int = Field(default=0, description="Tham số lệnh")

class GatewayCommandResponse(BaseModel):
    success: bool
    message: str
    command_sent: dict
