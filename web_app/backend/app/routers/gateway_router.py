import json
import os
import re
import hashlib
import time
from datetime import datetime, timezone
from fastapi import APIRouter, Depends, HTTPException, Query, Request, UploadFile, File, Form
from fastapi.responses import FileResponse
from sqlmodel import Session, select, desc
from typing import List

from app.database import get_session
from app.auth import get_current_user
from app.models import (
    RelayControlRequest,
    NodeCommandRequest,
    OTATriggerRequest,
    GatewayCommandResponse,
    GatewayStatusLog,
    ControlAudit
)
from app.mqtt_service import latest_gateway_status, latest_sensor_data, publish_command, publish_ota_command
from app.hunonic_service import hunonic_service

FIRMWARE_DIR = os.getenv("FIRMWARE_DIR")
if not FIRMWARE_DIR:
    if os.path.exists("/app/firmware_storage"):
        FIRMWARE_DIR = "/app/firmware_storage"
    else:
        FIRMWARE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../firmware_storage"))
os.makedirs(FIRMWARE_DIR, exist_ok=True)
UPLOAD_DIR = FIRMWARE_DIR

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

    # Cập nhật trạng thái trực tuyến thực tế của thiết bị Điều hòa qua trường ir_online của Gateway
    hun_info = hunonic_service.get_status()
    is_ac_online = hun_info.get("is_online", False)

    # QUY TẮC CỐT LÕI: Khi Gateway ngoại tuyến hoặc IR Hub chưa cắm nguồn -> Ngoại tuyến!
    if not gw_online:
        latest_gateway_status["ac_online"] = False
        latest_gateway_status["ac_offline_reason"] = "Trạm Gateway ESP32-S3 đang ngoại tuyến (Offline)"
    else:
        latest_gateway_status["ac_online"] = is_ac_online
        latest_gateway_status["ac_offline_reason"] = hun_info.get("reason")

    latest_gateway_status["ac_hub_state"] = hun_info.get("hub_state", "2")
    latest_gateway_status["ac_hub_last_online"] = hun_info.get("hub_last_online")

    # Đồng bộ trạng thái online vào từng thiết bị con
    if "ac_devices" in latest_gateway_status and isinstance(latest_gateway_status["ac_devices"], dict):
        for did, dinfo in latest_gateway_status["ac_devices"].items():
            dinfo["online"] = latest_gateway_status["ac_online"]
            if not latest_gateway_status["ac_online"]:
                dinfo["power"] = 0

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

    # Chỉ thêm IR Hub vào danh sách phát hiện nếu thực tế thiết bị đang trực tuyến
    if is_hub_online:
        devices.append({
            "id": f"ir_hub_{hub_id}",
            "name": hub_name,
            "type": "ir_hub",
            "source": "Hunonic Cloud / IR Smart Pro",
            "device_id": hub_id,
            "online": True,
            "details": f"ID: {hub_id} • {len(children)} thiết bị IR kết nối • Trực tuyến",
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
            seconds_ago = (datetime.now(timezone.utc) - s_dt).total_seconds()
            node_online = (seconds_ago <= 60)
            if node_online:
                val2_val = latest_sensor_data.get('val2', 0)
                val2_label = "Áp suất" if val2_val > 120 else "Độ ẩm"
                val2_unit = "hPa" if val2_val > 120 else "%"
                devices.append({
                    "id": f"node_espnow_{latest_sensor_data.get('node_id', 1)}",
                    "name": f"Node Cảm Biến #{latest_sensor_data.get('node_id', 1)} (ESP-NOW)",
                    "type": "sensor",
                    "source": "ESP-NOW 2.4GHz",
                    "mac": latest_sensor_data.get("mac", "ESP-NOW"),
                    "online": True,
                    "seconds_ago": max(0, int(seconds_ago)),
                    "details": f"Nhiệt độ: {latest_sensor_data.get('val1', 0):.1f}°C • {val2_label}: {val2_val:.1f} {val2_unit} • Pin: {latest_sensor_data.get('battery', 0):.2f}V"
                })
        except Exception:
            pass

    return {
        "success": True,
        "data": devices
    }

# ==============================================================================
# QUẢN LÝ & CẬP NHẬT FIRMWARE TỪ XA QUA CLOUD (FIRMWARE OTA ENGINE - CHUẨN PUMP)
# ==============================================================================

@router.post("/ota/upload", summary="Tải lên file firmware (.bin)")
async def upload_firmware(
    file: UploadFile = File(...),
    current_user: dict = Depends(get_current_user)
):
    if not file.filename.endswith(".bin"):
        raise HTTPException(status_code=400, detail="Chỉ chấp nhận file định dạng firmware (.bin)!")

    # Đặt tên file an toàn
    safe_filename = os.path.basename(file.filename)
    file_path = os.path.join(FIRMWARE_DIR, safe_filename)

    content = await file.read()
    file_size = len(content)

    if file_size == 0:
        raise HTTPException(status_code=400, detail="File firmware rỗng (0 bytes)!")

    # Tính MD5 checksum
    md5_hash = hashlib.md5(content).hexdigest()

    with open(file_path, "wb") as f:
        f.write(content)

    return {
        "filename": safe_filename,
        "size": file_size,
        "md5": md5_hash,
        "download_url": f"/firmware/{safe_filename}",
        "uploaded_at": int(time.time()),
        "success": True
    }

@router.get("/ota/firmwares", summary="Danh sách firmware có sẵn trên server")
def list_firmwares(current_user: dict = Depends(get_current_user)):
    files = []
    if os.path.exists(FIRMWARE_DIR):
        for f in os.listdir(FIRMWARE_DIR):
            if f.endswith(".bin"):
                fp = os.path.join(FIRMWARE_DIR, f)
                stat = os.stat(fp)
                files.append({
                    "filename": f,
                    "size": stat.st_size,
                    "download_url": f"/firmware/{f}",
                    "modified_at": int(stat.st_mtime)
                })
    files.sort(key=lambda x: x["modified_at"], reverse=True)
    return files

@router.post("/ota/trigger", summary="Bắn lệnh OTA qua MQTT tới ESP32")
def trigger_ota(
    payload: OTATriggerRequest,
    current_user: dict = Depends(get_current_user),
    session: Session = Depends(get_session)
):
    topic = "gate/ota"
    ota_data = {
        "action": "ota",
        "cmd": "ota",
        "version": payload.version or "1.0.1",
        "target": payload.target,       # "esp32s3_gate" hoặc "esp32c3_node"
        "url": payload.url,
        "md5": payload.md5 or "",
        "size": payload.size or 0,
        "filename": payload.filename or ""
    }

    # Bắn MQTT không khoảng trắng để an toàn với mọi parser C
    payload_str = json.dumps(ota_data, separators=(',', ':'))
    publish_ota_command(ota_data)

    latest_gateway_status["ota"]["target"] = payload.target
    latest_gateway_status["ota"]["status"] = "in_progress"
    latest_gateway_status["ota"]["percent"] = 0
    latest_gateway_status["ota"]["bytes"] = 0
    latest_gateway_status["ota"]["total"] = payload.size or 0
    latest_gateway_status["ota"]["version"] = payload.version
    latest_gateway_status["ota"]["error"] = None
    latest_gateway_status["ota"]["message"] = "Đang kích hoạt nạp OTA..."
    latest_gateway_status["ota"]["timestamp"] = int(time.time())

    print(f"🚀 [Gateway OTA] Đã bắn lệnh OTA [{topic}]: {payload_str}")

    audit = ControlAudit(
        username=current_user["username"],
        target=payload.target,
        command="ota_trigger",
        payload=payload_str,
        status="sent"
    )
    session.add(audit)
    session.commit()

    return {
        "status": "success",
        "success": True,
        "message": f"Đã gửi lệnh OTA thành công tới mục tiêu: {payload.target}!",
        "ota_payload": ota_data
    }

@router.get("/ota/progress", summary="Xem tiến độ nạp OTA theo thời gian thực (%)")
def get_ota_progress(current_user: dict = Depends(get_current_user)):
    return latest_gateway_status.get("ota", {
        "target": "esp32s3_gate",
        "status": "idle",
        "percent": 0,
        "bytes": 0,
        "total": 0,
        "version": "1.0.0",
        "error": None,
        "message": "Sẵn sàng"
    })

@router.get("/ota/status", summary="Xem trạng thái OTA và Telemetry thiết bị")
def get_ota_status(current_user: dict = Depends(get_current_user)):
    gw_online = latest_gateway_status.get("online", False)
    return {
        "success": True,
        "gateway_online": gw_online,
        "gateway_ver": latest_gateway_status.get("fw_ver", "1.0.0"),
        "node_ver": latest_sensor_data.get("fw", "1.0.0"),
        "uptime_sec": latest_gateway_status.get("uptime_sec", 0),
        "free_heap": latest_gateway_status.get("free_heap", 0),
        "rssi": latest_gateway_status.get("rssi", -100),
        "node_id": latest_sensor_data.get("node_id", 1),
        "node_battery": latest_sensor_data.get("battery", 0.0),
        "total_node_pkts": latest_gateway_status.get("total_node_pkts", 0),
        "ota": latest_gateway_status.get("ota", {})
    }

@router.get("/ota/files/{filename}", summary="Tải về file Firmware (.bin)", include_in_schema=False)
def download_firmware_file(filename: str):
    safe_name = os.path.basename(filename)
    file_path = os.path.join(FIRMWARE_DIR, safe_name)
    if not os.path.exists(file_path):
        raise HTTPException(status_code=404, detail="File firmware không tồn tại trên máy chủ!")
    return FileResponse(
        path=file_path,
        media_type="application/octet-stream",
        filename=safe_name
    )
