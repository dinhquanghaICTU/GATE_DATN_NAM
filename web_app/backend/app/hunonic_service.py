import logging
from app.mqtt_service import latest_gateway_status

logger = logging.getLogger("HUNONIC_SERVICE")

class HunonicService:
    def __init__(self, phone: str = "0358868830", password: str = "Zdinhquangha1234@"):
        self.phone = phone
        self.password = password
        self.token = None

    def login(self) -> bool:
        """
        Đã vô hiệu hóa login chủ động từ Backend Server để tránh chiếm quyền (revoke token)
        của trạm Gateway ESP32-S3 đang kết nối 24/7 tới WebSocket của Hunonic Cloud.
        """
        logger.info("Backend sử dụng kết nối trực tiếp qua ESP32 Gateway, không login trùng lặp lên Cloud.")
        return True

    def get_status(self) -> dict:
        """
        Kiểm tra trạng thái thời gian thực của thiết bị Hunonic IR và Điều hòa.
        Đọc trực tiếp từ bản tin Heartbeat và Telemetry thời gian thực của Gateway ESP32-S3.
        KHÔNG gọi REST API web.hunonic.com để loại bỏ triệt để xung đột tài khoản (Account Session Conflict)!
        """
        gw_online = latest_gateway_status.get("online", False)
        ws_conn = bool(latest_gateway_status.get("ws_connected", 0))
        is_online = gw_online and ws_conn

        if not gw_online:
            reason = "Trạm Gateway ESP32-S3 đang ngoại tuyến (Offline)"
        elif not ws_conn:
            reason = "Trạm phát hồng ngoại IR Hub đang ngắt kết nối WebSocket tới Hunonic"
        else:
            reason = "Trực tuyến (Online)"

        ac_count = latest_gateway_status.get("ac_count")
        ac_id = str(latest_gateway_status.get("ac_id") or "")
        ac_name = str(latest_gateway_status.get("ac_name") or "")

        # Lấy danh sách toàn bộ các thiết bị con từ RAM cache (nhiều điều hòa trên 1 trạm IR)
        ac_devices_dict = latest_gateway_status.get("ac_devices", {})
        children = []

        if ac_devices_dict:
            for did, dinfo in ac_devices_dict.items():
                p = dinfo.get("power", 0) if is_online else 0
                t = dinfo.get("temp", 25)
                m = dinfo.get("mode", 2)
                f = dinfo.get("fan", 0)
                b = dinfo.get("brand", 117)
                dname = dinfo.get("name") or f"Điều hòa {did}"
                children.append({
                    "id": str(did),
                    "name": dname,
                    "type": "ac",
                    "category": "AC",
                    "brand": b,
                    "power": p,
                    "temp": t,
                    "mode": m,
                    "fan": f,
                    "online": is_online,
                    "offline_reason": reason,
                    "details": f"ID: {did} • Nhiệt độ: {t}°C"
                })
        elif ac_count != 0 and ac_id:
            ac_power = latest_gateway_status.get("ac_power", 0) if is_online else 0
            ac_temp = latest_gateway_status.get("ac_temp", 25)
            ac_mode = latest_gateway_status.get("ac_mode", 2)
            ac_fan = latest_gateway_status.get("ac_fan", 0)
            ac_brand = latest_gateway_status.get("ac_brand", 117)

            children.append({
                "id": ac_id,
                "name": ac_name if ac_name else "Điều hoà phòng Hương",
                "type": "ac",
                "category": "AC",
                "brand": ac_brand,
                "power": ac_power,
                "temp": ac_temp,
                "mode": ac_mode,
                "fan": ac_fan,
                "online": is_online,
                "offline_reason": reason,
                "details": f"ID: {ac_id} • Nhiệt độ: {ac_temp}°C"
            })

        return {
            "success": True,
            "is_online": is_online,
            "hub_name": "IR Hunonic Smart Pro",
            "hub_id": "3987670",
            "hub_state": "1" if is_online else "2",
            "hub_last_online": latest_gateway_status.get("last_seen"),
            "children": children,
            "children_count": len(children),
            "ac_id": ac_id,
            "ac_name": ac_name,
            "ac_power": latest_gateway_status.get("ac_power", 0) if is_online else 0,
            "ac_temp": latest_gateway_status.get("ac_temp", 32),
            "reason": reason
        }

hunonic_service = HunonicService()
