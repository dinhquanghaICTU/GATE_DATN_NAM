import logging
from app.mqtt_service import latest_gateway_status

logger = logging.getLogger("HUNONIC_SERVICE")

class HunonicService:
    def __init__(self):
        """
        Khởi tạo HunonicService.
        Theo kiến trúc dự án: Web Backend KHÔNG gọi trực tiếp tới Hunonic Cloud.
        Trạm Gateway ESP32-S3 đảm nhiệm việc định kỳ kiểm tra trạng thái IR Hub thực tế
        từ Hunonic Cloud và gửi trường 'ir_online' lên MQTT Broker.
        """
        pass

    def get_status(self) -> dict:
        """
        Kiểm tra trạng thái thời gian thực của thiết bị Hunonic IR và Điều hòa.
        Được lấy trực tiếp từ bản tin Heartbeat của Gateway ESP32-S3 (trường ir_online).
        """
        gw_online = latest_gateway_status.get("online", False)
        ir_online = gw_online and bool(latest_gateway_status.get("ir_online", 0))

        if not gw_online:
            reason = "Trạm Gateway ESP32-S3 đang ngoại tuyến (Offline)"
        elif not ir_online:
            reason = "Trạm phát hồng ngoại IR Hub chưa cắm nguồn (Ngoại tuyến)"
        else:
            reason = "Trực tuyến (Online)"

        hub_state = "1" if ir_online else "2"

        ac_count = latest_gateway_status.get("ac_count")
        ac_id = str(latest_gateway_status.get("ac_id") or "")
        ac_name = str(latest_gateway_status.get("ac_name") or "")

        # Lấy danh sách toàn bộ các thiết bị con từ RAM cache
        ac_devices_dict = latest_gateway_status.get("ac_devices", {})
        children = []

        if ac_devices_dict:
            for did, dinfo in ac_devices_dict.items():
                p = dinfo.get("power", 0) if ir_online else 0
                t = dinfo.get("temp", 25)
                m = dinfo.get("mode", 2)
                f = dinfo.get("fan", 0)
                b = dinfo.get("brand", 0)
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
                    "online": ir_online,
                    "offline_reason": reason,
                    "details": f"ID: {did} • Nhiệt độ: {t}°C"
                })
        elif ac_count != 0 and ac_id:
            ac_power = latest_gateway_status.get("ac_power", 0) if ir_online else 0
            ac_temp = latest_gateway_status.get("ac_temp", 25)
            ac_mode = latest_gateway_status.get("ac_mode", 2)
            ac_fan = latest_gateway_status.get("ac_fan", 0)
            ac_brand = latest_gateway_status.get("ac_brand", 0)

            children.append({
                "id": ac_id,
                "name": ac_name if ac_name else f"Điều hòa {ac_id}",
                "type": "ac",
                "category": "AC",
                "brand": ac_brand,
                "power": ac_power,
                "temp": ac_temp,
                "mode": ac_mode,
                "fan": ac_fan,
                "online": ir_online,
                "offline_reason": reason,
                "details": f"ID: {ac_id} • Nhiệt độ: {ac_temp}°C"
            })
        else:
            # Không có thiết bị điều hòa con nào trong trạm IR
            children = []

        return {
            "success": True,
            "is_online": ir_online,
            "hub_name": "IR trung tâm",
            "hub_id": "3987670",
            "hub_state": hub_state,
            "hub_last_online": latest_gateway_status.get("last_seen"),
            "children": children,
            "children_count": len(children),
            "ac_id": ac_id,
            "ac_name": ac_name,
            "ac_power": latest_gateway_status.get("ac_power", 0) if ir_online else 0,
            "ac_temp": latest_gateway_status.get("ac_temp", 25),
            "reason": reason
        }

hunonic_service = HunonicService()
