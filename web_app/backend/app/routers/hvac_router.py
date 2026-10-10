import json
from fastapi import APIRouter, Depends, HTTPException
from sqlmodel import Session

from app.database import get_session
from app.auth import get_current_user
from app.models import HVACControlRequest, GatewayCommandResponse, ControlAudit
from app.mqtt_service import latest_gateway_status, publish_command
from app.hunonic_service import hunonic_service

router = APIRouter(prefix="/hvac", tags=["3. Điều khiển Điều Hòa Không Khí (HVAC / Hunonic IR)"])

@router.get(
    "/status",
    summary="Lấy trạng thái tức thời của Điều Hòa",
    description="Truy xuất trạng thái hoạt động hiện tại (Bật/Tắt, Nhiệt độ cài đặt, Chế độ, Tốc độ quạt, Trực tuyến/Ngoại tuyến) của Điều hòa Funiki/Hunonic."
)
def get_hvac_status(current_user: dict = Depends(get_current_user)):
    gw_online = latest_gateway_status.get("online", False)
    hun_info = hunonic_service.get_status()
    is_online = gw_online and hun_info.get("is_online", False)
    children = hun_info.get("children", [])
    if not gw_online:
        for c in children:
            c["online"] = False
            c["power"] = 0
            c["offline_reason"] = "Trạm Gateway ESP32-S3 đang ngoại tuyến (Offline)"

    return {
        "success": True,
        "data": {
            "id": latest_gateway_status.get("ac_id") or hun_info.get("ac_id", "4000261"),
            "name": latest_gateway_status.get("ac_name") or hun_info.get("ac_name", "Điều hoà phòng Hương"),
            "power": latest_gateway_status.get("ac_power", 0) if gw_online else 0,
            "temp": latest_gateway_status.get("ac_temp", 25),
            "ws_connected": latest_gateway_status.get("ws_connected", 0),
            "online": is_online,
            "offline_reason": "Trạm Gateway ESP32-S3 đang ngoại tuyến (Offline)" if not gw_online else hun_info.get("reason", "Ngoại tuyến"),
            "hub_state": hun_info.get("hub_state"),
            "hub_last_online": hun_info.get("hub_last_online"),
            "children": children
        }
    }

@router.get(
    "/devices",
    summary="Lấy danh sách các thiết bị con kết nối vào Trạm IR Hunonic",
    description="Truy vấn toàn bộ thiết bị IR (Điều hòa, Tivi, Quạt...) mà trạm IR Hunonic Smart Pro quản lý."
)
def get_hvac_devices(current_user: dict = Depends(get_current_user)):
    gw_online = latest_gateway_status.get("online", False)
    hun_info = hunonic_service.get_status()
    is_online = gw_online and hun_info.get("is_online", False)
    children = hun_info.get("children", [])
    if not gw_online:
        for c in children:
            c["online"] = False
            c["power"] = 0
            c["offline_reason"] = "Trạm Gateway ESP32-S3 đang ngoại tuyến (Offline)"

    return {
        "success": True,
        "data": {
            "hub_id": hun_info.get("hub_id", "3987670"),
            "hub_name": hun_info.get("hub_name", "IR Hunonic Smart Pro"),
            "online": is_online,
            "offline_reason": "Trạm Gateway ESP32-S3 đang ngoại tuyến (Offline)" if not gw_online else hun_info.get("reason"),
            "children": children
        }
    }

@router.post(
    "/sync",
    summary="Yêu cầu Gateway đồng bộ lại danh sách thiết bị từ Hunonic Cloud",
    description="Gửi lệnh MQTT 'sync' xuống Gateway để gọi HTTP listDeviceByHome và cập nhật danh sách thiết bị mới nhất."
)
def sync_hvac_devices(current_user: dict = Depends(get_current_user)):
    gw_online = latest_gateway_status.get("online", False)
    if not gw_online:
        raise HTTPException(
            status_code=400,
            detail="Không thể đồng bộ: Trạm Gateway ESP32-S3 đang ngoại tuyến (Offline)!"
        )

    # Gửi lệnh sync xuống Gateway
    publish_command({"cmd": "sync"})

    import time
    time.sleep(1.5)

    return get_hvac_devices(current_user=current_user)

@router.post(
    "/control",
    response_model=GatewayCommandResponse,
    summary="Điều khiển Điều Hòa (Bật/Tắt, Nhiệt độ, Quạt, Chế độ)",
    description="Phát lệnh điều khiển qua MQTT Broker tới Gateway nếu thiết bị đang Online. Nếu Offline sẽ từ chối lệnh."
)
def control_hvac(
    req: HVACControlRequest,
    current_user: dict = Depends(get_current_user),
    session: Session = Depends(get_session)
):
    # 1. Kiểm tra Gateway có online không (in-memory RAM < 0.01ms)
    gw_online = latest_gateway_status.get("online", False)
    if not gw_online:
        raise HTTPException(
            status_code=400,
            detail="Không thể điều khiển: Trạm Gateway ESP32-S3 đang ngoại tuyến (Offline)!"
        )

    # 2. Kiểm tra trạng thái IR Hub từ RAM cache (tránh gọi HTTP sang Hunonic Cloud gây trễ 0.5s - 1.5s)
    ac_online = latest_gateway_status.get("ac_online", False)
    if not ac_online:
        hun_info = hunonic_service.get_status()
        if not hun_info.get("is_online", False):
            reason = hun_info.get("reason", "Thiết bị hoặc trạm hồng ngoại đang ngoại tuyến!")
            raise HTTPException(
                status_code=400,
                detail=f"Không thể điều khiển: {reason}"
            )

    dev_id = req.device_id.strip() if req.device_id else latest_gateway_status["ac_id"]
    if not dev_id:
        dev_id = "4000261"

    cmd = {
        "cmd": "hunonic_ac",
        "id": dev_id,
        "power": req.power,
        "temp": req.temp,
        "mode": req.mode,
        "fan": req.fan
    }

    success = publish_command(cmd)
    if not success:
        raise HTTPException(status_code=500, detail="Lỗi gửi lệnh điều khiển điều hòa qua MQTT!")

    # Cập nhật dự đoán vào RAM
    latest_gateway_status["ac_power"] = req.power
    latest_gateway_status["ac_temp"] = req.temp
    latest_gateway_status["ac_mode"] = req.mode
    latest_gateway_status["ac_fan"] = req.fan

    # Ghi nhật ký điều khiển
    mode_names = {0: "Auto", 1: "Cool", 2: "Heat", 3: "Dry", 4: "Fan"}
    fan_names = {0: "Auto", 1: "Min", 3: "Med", 5: "Max"}
    action_desc = f"{'BẬT' if req.power == 1 else 'TẮT'} | {req.temp}°C | Mode: {mode_names.get(req.mode, req.mode)} | Fan: {fan_names.get(req.fan, req.fan)}"

    audit = ControlAudit(
        username=current_user["username"],
        target=f"hvac_{dev_id}",
        command=action_desc,
        payload=json.dumps(cmd),
        status="sent"
    )
    session.add(audit)
    session.commit()

    return GatewayCommandResponse(
        success=True,
        message=f"Đã phát lệnh điều khiển điều hòa: {action_desc}",
        command_sent=cmd
    )

@router.post(
    "/sync",
    response_model=GatewayCommandResponse,
    summary="Yêu cầu Gateway đồng bộ danh sách thiết bị Hunonic",
    description="Gửi lệnh 'get_devices' tới Gateway để Gateway gọi API HTTP của Hunonic làm mới danh sách thiết bị và đăng ký lại WebSocket."
)
def sync_hvac_devices(
    current_user: dict = Depends(get_current_user),
    session: Session = Depends(get_session)
):
    cmd = {"cmd": "get_devices"}
    success = publish_command(cmd)
    if not success:
        raise HTTPException(status_code=500, detail="Lỗi gửi lệnh đồng bộ qua MQTT!")

    audit = ControlAudit(
        username=current_user["username"],
        target="hvac_bridge",
        command="sync_devices",
        payload=json.dumps(cmd),
        status="sent"
    )
    session.add(audit)
    session.commit()

    return GatewayCommandResponse(
        success=True,
        message="Đã gửi yêu cầu đồng bộ thiết bị tới Gateway thành công!",
        command_sent=cmd
    )
