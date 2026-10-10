import json
from datetime import datetime, timezone
from fastapi import APIRouter, Depends, HTTPException, Query
from sqlmodel import Session, select, desc
from typing import List

from app.database import get_session
from app.auth import get_current_user
from app.models import (
    RelayControlRequest,
    NodeCommandRequest,
    GatewayCommandResponse,
    GatewayStatusLog,
    ControlAudit
)
from app.mqtt_service import latest_gateway_status, latest_sensor_data, publish_command
from app.hunonic_service import hunonic_service

router = APIRouter(prefix="/gateway", tags=["2. Điều khiển Trạm Gateway (ESP32-S3)"])

@router.get(
    "/status",
    summary="Lấy trạng thái tức thời của Trạm Gateway",
    description="Truy vấn dữ liệu thời gian thực từ trạm ESP32-S3 (Uptime, RAM, Sóng Wi-Fi, Trạng thái Relay, Kết nối Hunonic, Firmware...)."
)
def get_gateway_status(current_user: dict = Depends(get_current_user)):
    # Watchdog kiểm tra trạng thái Online: nếu quá 15s không nhận được heartbeat -> Đánh dấu Offline
    last_seen_str = latest_gateway_status.get("last_seen")
    if last_seen_str:
        try:
            last_dt = datetime.fromisoformat(last_seen_str)
            if last_dt.tzinfo is None:
                last_dt = last_dt.replace(tzinfo=timezone.utc)
            now = datetime.now(timezone.utc)
            if (now - last_dt).total_seconds() > 35:
                latest_gateway_status["online"] = False
        except Exception:
            latest_gateway_status["online"] = False
    else:
        latest_gateway_status["online"] = False

    gw_online = latest_gateway_status.get("online", False)

    # Cập nhật trạng thái trực tuyến thực tế của thiết bị Điều hòa từ Hunonic Cloud
    hun_info = hunonic_service.get_status()
    # QUY TẮC CỐT LÕI: Khi Gateway ngoại tuyến -> Tất cả thiết bị con qua Gateway bắt buộc ngoại tuyến!
    if not gw_online:
        latest_gateway_status["ac_online"] = False
        latest_gateway_status["ac_offline_reason"] = "Trạm Gateway ESP32-S3 đang ngoại tuyến (Offline)"
    else:
        latest_gateway_status["ac_online"] = hun_info.get("is_online", False)
        latest_gateway_status["ac_offline_reason"] = hun_info.get("reason")

    latest_gateway_status["ac_hub_state"] = hun_info.get("hub_state")
    latest_gateway_status["ac_hub_last_online"] = hun_info.get("hub_last_online")

    return {
        "success": True,
        "data": latest_gateway_status
    }

@router.post(
    "/relay",
    response_model=GatewayCommandResponse,
    summary="Điều khiển Bật / Tắt Relay Gateway",
    description="Gửi lệnh điều khiển ngõ ra Relay kênh 1 hoặc kênh 2 xuống vi điều khiển ESP32-S3 qua MQTT."
)
def control_relay(
    req: RelayControlRequest,
    current_user: dict = Depends(get_current_user),
    session: Session = Depends(get_session)
):
    cmd = {
        "cmd": "relay",
        "ch": req.channel,
        "state": req.state
    }
    success = publish_command(cmd)
    if not success:
        raise HTTPException(status_code=500, detail="Không thể gửi lệnh qua MQTT Broker!")

    # Cập nhật tạm thời vào bộ nhớ đệm
    if req.channel == 1: latest_gateway_status["relay1"] = req.state
    elif req.channel == 2: latest_gateway_status["relay2"] = req.state

    # Ghi nhật ký điều khiển
    audit = ControlAudit(
        username=current_user["username"],
        target=f"relay_{req.channel}",
        command=f"relay_turn_{'on' if req.state == 1 else 'off'}",
        payload=json.dumps(cmd),
        status="sent"
    )
    session.add(audit)
    session.commit()

    return GatewayCommandResponse(
        success=True,
        message=f"Đã phát lệnh {'BẬT' if req.state == 1 else 'TẮT'} Relay kênh {req.channel} thành công!",
        command_sent=cmd
    )

@router.post(
    "/reboot",
    response_model=GatewayCommandResponse,
    summary="Khởi động lại Trạm Gateway từ xa",
    description="Gửi lệnh reboot vi điều khiển ESP32-S3."
)
def reboot_gateway(
    current_user: dict = Depends(get_current_user),
    session: Session = Depends(get_session)
):
    cmd = {"cmd": "reboot"}
    success = publish_command(cmd)
    if not success:
        raise HTTPException(status_code=500, detail="Không thể gửi lệnh reboot qua MQTT Broker!")

    audit = ControlAudit(
        username=current_user["username"],
        target="esp32s3_gateway",
        command="reboot",
        payload=json.dumps(cmd),
        status="sent"
    )
    session.add(audit)
    session.commit()

    return GatewayCommandResponse(
        success=True,
        message="Đã gửi lệnh khởi động lại Gateway thành công! Thiết bị sẽ khởi động lại sau 1 giây.",
        command_sent=cmd
    )

@router.post(
    "/node_cmd",
    response_model=GatewayCommandResponse,
    summary="Gửi lệnh không dây xuống Node cảm biến (ESP-NOW)",
    description="Gửi lệnh điều khiển từ Cloud xuống Gateway, Gateway sẽ bắn tiếp qua ESP-NOW tới Node C3."
)
def send_node_command(
    req: NodeCommandRequest,
    current_user: dict = Depends(get_current_user),
    session: Session = Depends(get_session)
):
    cmd = {
        "cmd": "node_cmd",
        "node_id": req.node_id,
        "cmd_id": req.cmd_id,
        "param": req.param
    }
    success = publish_command(cmd)
    if not success:
        raise HTTPException(status_code=500, detail="Lỗi gửi lệnh qua MQTT")

    audit = ControlAudit(
        username=current_user["username"],
        target=f"sensor_node_{req.node_id}",
        command=f"node_cmd_{req.cmd_id}",
        payload=json.dumps(cmd),
        status="sent"
    )
    session.add(audit)
    session.commit()

    return GatewayCommandResponse(
        success=True,
        message=f"Đã chuyển lệnh xuống Node #{req.node_id} qua ESP-NOW thành công!",
        command_sent=cmd
    )

@router.get(
    "/logs",
    summary="Xem lịch sử Heartbeat Gateway",
    description="Lấy danh sách các bản tin trạng thái gần nhất của Gateway từ Database."
)
def get_gateway_logs(
    limit: int = Query(default=50, le=200),
    session: Session = Depends(get_session),
    current_user: dict = Depends(get_current_user)
):
    logs = session.exec(
        select(GatewayStatusLog).order_by(desc(GatewayStatusLog.created_at)).limit(limit)
    ).all()
    return logs

@router.get(
    "/discovered",
    summary="Lấy danh sách các thiết bị phát hiện được từ Gateway và Cloud",
    description="Truy vấn danh sách thiết bị sẵn sàng kết nối: Điều hòa Hunonic Cloud/IR, Node cảm biến ESP-NOW, và Relay tích hợp Gateway."
)
def get_discovered_devices(current_user: dict = Depends(get_current_user)):
    devices = []

    gw_online = latest_gateway_status.get("online", False)

    # 1. Trạm Hồng Ngoại IR Hunonic Smart Pro (Hub chứa danh sách các thiết bị IR con)
    hun_info = hunonic_service.get_status()
    hub_id = hun_info.get("hub_id", "3987670")
    hub_name = hun_info.get("hub_name", "IR Hunonic Smart Pro")
    is_hub_online = gw_online and hun_info.get("is_online", False)
    children = hun_info.get("children", [])
    if not gw_online:
        for c in children:
            c["online"] = False
            c["power"] = 0
            c["offline_reason"] = "Trạm Gateway ESP32-S3 đang ngoại tuyến (Offline)"

    devices.append({
        "id": f"ir_hub_{hub_id}",
        "name": hub_name,
        "type": "ir_hub",
        "source": "Hunonic Cloud / IR Smart Pro",
        "device_id": hub_id,
        "online": is_hub_online,
        "offline_reason": "Trạm Gateway ESP32-S3 đang ngoại tuyến (Offline)" if not gw_online else hun_info.get("reason", "Ngoại tuyến"),
        "details": f"ID: {hub_id} • {len(children)} thiết bị IR kết nối • {'Trực tuyến' if is_hub_online else 'Ngoại tuyến'}",
        "children": children
    })

    # 2. Node Cảm Biến ESP-NOW: CHỈ HIỆN KHI THỰC SỰ CÓ BẢN TIN NHẬN ĐƯỢC TỪ NODE C3
    last_sensor_seen = latest_sensor_data.get("last_seen")
    total_node_pkts = latest_gateway_status.get("total_node_pkts", 0)
    has_real_sensor = (
        last_sensor_seen is not None and 
        (total_node_pkts > 0 or latest_sensor_data.get("val1", 0) > 0 or latest_sensor_data.get("val2", 0) > 0 or latest_sensor_data.get("battery", 0) > 0)
    )
    if has_real_sensor:
        try:
            s_dt = datetime.fromisoformat(last_sensor_seen)
            if s_dt.tzinfo is None:
                s_dt = s_dt.replace(tzinfo=timezone.utc)
            if (datetime.now(timezone.utc) - s_dt).total_seconds() <= 120:
                devices.append({
                    "id": f"node_espnow_{latest_sensor_data.get('node_id', 1)}",
                    "name": f"Node Cảm Biến #{latest_sensor_data.get('node_id', 1)} (ESP-NOW)",
                    "type": "sensor",
                    "source": "ESP-NOW 2.4GHz",
                    "mac": latest_sensor_data.get("mac", "ESP-NOW"),
                    "online": True,
                    "details": f"Nhiệt độ: {latest_sensor_data.get('val1', 0):.1f}°C • Độ ẩm: {latest_sensor_data.get('val2', 0):.1f}% • Pin: {latest_sensor_data.get('battery', 0):.2f}V"
                })
        except Exception:
            pass

    # 3. Relay Kênh 1 On-board Gateway
    devices.append({
        "id": "relay_ch_1",
        "name": "Công Tắc Relay 1",
        "type": "relay",
        "source": "Ngõ Ra Gateway (GPIO10)",
        "channel": 1,
        "online": latest_gateway_status.get("online", False),
        "state": latest_gateway_status.get("relay1", 0),
        "details": "Chân điều khiển GPIO10 • Tải 220V/10A"
    })

    # 4. Relay Kênh 2 On-board Gateway
    devices.append({
        "id": "relay_ch_2",
        "name": "Công Tắc Relay 2",
        "type": "relay",
        "source": "Ngõ Ra Gateway (GPIO11)",
        "channel": 2,
        "online": latest_gateway_status.get("online", False),
        "state": latest_gateway_status.get("relay2", 0),
        "details": "Chân điều khiển GPIO11 • Tải 220V/10A"
    })

    return {
        "success": True,
        "data": devices
    }
