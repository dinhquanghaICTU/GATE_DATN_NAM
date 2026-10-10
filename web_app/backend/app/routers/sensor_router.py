from fastapi import APIRouter, Depends, Query
from sqlmodel import Session, select, desc
from typing import List, Optional

from app.database import get_session
from app.auth import get_current_user
from app.models import SensorRecord
from app.mqtt_service import latest_sensor_data

router = APIRouter(prefix="/sensors", tags=["4. Cảm biến Môi trường & Hiện diện (Sensors & ESP-NOW)"])

@router.get(
    "/latest",
    summary="Lấy dữ liệu cảm biến mới nhất tức thời",
    description="Truy xuất bản tin cảm biến mới nhất từ Node ESP32-C3 qua sóng không dây ESP-NOW (Nhiệt độ, Độ ẩm/Hiện diện, Pin, RSSI)."
)
def get_latest_sensor_data(current_user: dict = Depends(get_current_user)):
    return {
        "success": True,
        "data": latest_sensor_data
    }

@router.get(
    "/history",
    response_model=List[SensorRecord],
    summary="Lấy lịch sử dữ liệu cảm biến từ Database",
    description="Truy vấn dữ liệu lịch sử đo đạc cảm biến để vẽ đồ thị theo thời gian hoặc trích xuất báo cáo."
)
def get_sensor_history(
    node_id: Optional[int] = Query(default=None, description="Lọc theo ID Node (ví dụ: 1)"),
    limit: int = Query(default=100, le=500, description="Số lượng bản ghi tối đa"),
    session: Session = Depends(get_session),
    current_user: dict = Depends(get_current_user)
):
    query = select(SensorRecord)
    if node_id is not None:
        query = query.where(SensorRecord.node_id == node_id)
    
    query = query.order_by(desc(SensorRecord.created_at)).limit(limit)
    records = session.exec(query).all()
    return records
