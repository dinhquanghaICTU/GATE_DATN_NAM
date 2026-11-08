import json
import ssl
import logging
from urllib.parse import urlparse
import paho.mqtt.client as mqtt
from sqlmodel import Session
from datetime import datetime

from app.config import settings
from app.database import engine
from app.models import GatewayStatusLog, SensorRecord

logger = logging.getLogger("MQTT_SERVICE")

# Bộ nhớ đệm lưu trạng thái mới nhất nhận từ MQTT (phục vụ API truy vấn tức thời < 1ms)
latest_gateway_status = {
    "device": "esp32s3_gate",
    "online": False,
    "last_seen": None,
    "uptime_sec": 0,
    "free_heap": 0,
    "rssi": -100,
    "ac_id": "",
    "ac_name": "",
    "ac_brand": 0,
    "ac_power": 0,
    "ac_temp": 25,
    "ac_mode": 1,
    "ac_fan": 0,
    "ac_devices": {},
    "ws_connected": 0,
    "ir_online": 0,
    "ac_online": False,
    "ac_offline_reason": "Chưa nhận được dữ liệu từ Gateway",
    "ac_hub_state": "2",
    "total_node_pkts": 0,
    "fw_ver": "1.0.0",
    "ota": {
        "status": "idle",
        "percent": 0,
        "bytes": 0,
        "total": 0,
        "target": "esp32s3_gate",
        "message": "Sẵn sàng",
        "updated_at": None
    }
}

current_ota_progress = latest_gateway_status["ota"]

latest_sensor_data = {
    "node_id": 1,
    "val1": 0.0,
    "val2": 0.0,
    "battery": 0.0,
    "presence": None,
    "rssi": -100,
    "last_seen": None
}


mqtt_client: mqtt.Client = None

def on_connect(client, userdata, *args, **kwargs):
    rc = args[1] if len(args) > 1 else (args[0] if len(args) > 0 else 0)
    logger.info("Kết nối HiveMQ Cloud MQTT Broker THÀNH CÔNG!")
    client.subscribe(settings.TOPIC_GATE_STATUS, qos=1)
    client.subscribe(settings.TOPIC_GATE_TELEMETRY, qos=0)
    logger.info(f"Đã subscribe các topic: [{settings.TOPIC_GATE_STATUS}], [{settings.TOPIC_GATE_TELEMETRY}]")

def on_disconnect(client, userdata, *args, **kwargs):
    logger.warning("Mất kết nối MQTT Broker! Đang tự động kết nối lại...")

from datetime import datetime, timezone

# None until a complete inventory or a zero/single-device heartbeat arrives.
_ac_inventory_ids = None

def on_message(client, userdata, msg):
    global _ac_inventory_ids
    try:
        topic = msg.topic
        payload_str = msg.payload.decode("utf-8")
        data = json.loads(payload_str)
        now = datetime.now(timezone.utc)

        # 1. Nhận bản tin trạng thái từ Gateway (Heartbeat / Relay / AC state / LWT Offline)
        if topic == settings.TOPIC_GATE_STATUS:
            evt = data.get("event")
            if evt in ("lwt_offline", "offline") or data.get("online") is False:
                latest_gateway_status["online"] = False
                logger.warning(f"Gateway ESP32-S3 đã ngắt kết nối (Offline)! [event={evt}]")
            elif evt == "heartbeat":
                ac_count = data.get("ac_count")
                if ac_count is not None:
                    latest_gateway_status["ac_count"] = ac_count

                ac_id_val = data.get("ac_id", "")
                ac_name_val = data.get("ac_name", "")
                if ac_count == 0:
                    ac_id_val = ""
                    ac_name_val = ""
                    latest_gateway_status["ac_devices"] = {}

                if ac_count == 0:
                    _ac_inventory_ids = set()
                    latest_gateway_status["ac_devices"] = {}
                elif ac_count is not None and _ac_inventory_ids is not None and ac_count != len(_ac_inventory_ids):
                    _ac_inventory_ids = None

                latest_gateway_status.update({
                    "online": True,
                    "last_seen": now.isoformat(),
                    "uptime_sec": data.get("uptime_sec", 0),
                    "free_heap": data.get("free_heap", 0),
                    "rssi": data.get("rssi", -100),
                    "ac_count": latest_gateway_status.get("ac_count", 0),
                    "ac_id": ac_id_val if ac_count is not None else data.get("ac_id", latest_gateway_status["ac_id"]),
                    "ac_name": ac_name_val if ac_count is not None else data.get("ac_name", latest_gateway_status["ac_name"]),
                    "ac_power": data.get("ac_power", 0),
                    "ac_temp": data.get("ac_temp", 25),
                    "ac_mode": data.get("ac_mode", latest_gateway_status["ac_mode"]),
                    "ac_fan": data.get("ac_fan", latest_gateway_status["ac_fan"]),
                    "ac_brand": data.get("ac_brand", latest_gateway_status["ac_brand"]),
                    "ws_connected": data.get("ws_connected", 0),
                    "ir_online": data.get("ir_online", 0),
                    "total_node_pkts": data.get("total_node_pkts", 0),
                    "fw_ver": data.get("fw_ver", "1.0.0")
                })

                # Kiểm tra tự động phát hiện Firmware mới boot lên thành công sau khi OTA Reboot (giống cơ chế Pump)
                new_fw = data.get("fw_ver")
                if new_fw and latest_gateway_status["ota"].get("status") in ("in_progress", "rebooting"):
                    tgt_ver = latest_gateway_status["ota"].get("version")
                    if tgt_ver and (new_fw == tgt_ver or new_fw != latest_gateway_status.get("fw_ver")):
                        latest_gateway_status["ota"]["status"] = "success"
                        latest_gateway_status["ota"]["percent"] = 100
                        latest_gateway_status["ota"]["message"] = f"Thiết bị đã kích hoạt firmware v{new_fw} thành công!"
                        logger.info(f"🎉 [OTA AUTO SUCCESS] Thiết bị xác nhận version mới v{new_fw}!")

                # A heartbeat also refreshes the first real AC, even if device_list
                # was published before this backend subscribed.
                if ac_id_val and ac_count != 0:
                    devices = latest_gateway_status.setdefault("ac_devices", {})
                    entry = devices.setdefault(ac_id_val, {"id": ac_id_val})
                    for field, source in (("name", "ac_name"), ("brand", "ac_brand"),
                                          ("power", "ac_power"), ("temp", "ac_temp"),
                                          ("mode", "ac_mode"), ("fan", "ac_fan")):
                        if source in data:
                            entry[field] = data[source]

                ir_online_bool = bool(data.get("ir_online", 0))
                latest_gateway_status["ac_online"] = ir_online_bool
                latest_gateway_status["ac_offline_reason"] = (
                    "Trực tuyến (Online)" if ir_online_bool
                    else "Trạm phát hồng ngoại IR Hub chưa cắm nguồn (Ngoại tuyến)"
                )
                latest_gateway_status["ac_hub_state"] = "1" if ir_online_bool else "2"

                if "ac_devices" in latest_gateway_status and isinstance(latest_gateway_status["ac_devices"], dict):
                    for did, dinfo in latest_gateway_status["ac_devices"].items():
                        dinfo["online"] = ir_online_bool
                        if not ir_online_bool:
                            dinfo["power"] = 0
                # Lưu log định kỳ vào DB
                try:
                    with Session(engine) as session:
                        log_entry = GatewayStatusLog(
                            device=data.get("device", "esp32s3_gate"),
                            uptime_sec=data.get("uptime_sec", 0),
                            free_heap=data.get("free_heap", 0),
                            rssi=data.get("rssi", -100),
                            relay1=data.get("relay1", 0),
                            relay2=data.get("relay2", 0),
                            ac_id=data.get("ac_id", ""),
                            ac_name=data.get("ac_name", ""),
                            ac_power=data.get("ac_power", 0),
                            ac_temp=data.get("ac_temp", 25),
                            ws_connected=data.get("ws_connected", 0),
                            total_node_pkts=data.get("total_node_pkts", 0),
                            fw_ver=data.get("fw_ver", "1.0.0"),
                            created_at=now
                        )
                        session.add(log_entry)
                        session.commit()
                except Exception as db_err:
                    logger.debug(f"Bỏ qua ghi log gateway: {db_err}")

            elif evt == "device_list":
                ac_count = data.get("ac_count", 0)
                latest_gateway_status["ac_count"] = ac_count

                # Cập nhật danh sách nhiều thiết bị nếu Gateway gửi mảng devices
                if "devices" in data and isinstance(data["devices"], list):
                    new_devices = {}
                    for d in data["devices"]:
                        did = str(d.get("id", ""))
                        if did:
                            new_devices[did] = {
                                "id": did,
                                "name": d.get("name") or f"Điều hòa {did}",
                                "brand": d.get("brand", 0),
                                "power": d.get("power", 0),
                                "temp": d.get("temp", 25),
                                "mode": d.get("mode", 1),
                                "fan": d.get("fan", 0),
                                "online": bool(latest_gateway_status.get("ac_online", False))
                            }
                    _ac_inventory_ids = set(new_devices)
                    latest_gateway_status["ac_devices"] = new_devices
                    latest_gateway_status["ac_count"] = len(new_devices)
                    if len(new_devices) == 0:
                        latest_gateway_status["ac_id"] = ""
                        latest_gateway_status["ac_name"] = ""
                elif ac_count == 0:
                    _ac_inventory_ids = set()
                    latest_gateway_status["ac_devices"] = {}
                    latest_gateway_status["ac_id"] = ""
                    latest_gateway_status["ac_name"] = ""

                if ac_count > 0 and "ac" in data:
                    ac = data["ac"]
                    aid = str(ac.get("id", ""))
                    latest_gateway_status["ac_id"] = aid
                    latest_gateway_status["ac_name"] = ac.get("name", "")
                    latest_gateway_status["ac_brand"] = ac.get("brand", 0)
                    latest_gateway_status["ac_power"] = ac.get("power", 0)
                    latest_gateway_status["ac_temp"] = ac.get("temp", 25)
                    latest_gateway_status["ac_mode"] = ac.get("mode", 2)
                    latest_gateway_status["ac_fan"] = ac.get("fan", 0)
                    if aid:
                        if "ac_devices" not in latest_gateway_status:
                            latest_gateway_status["ac_devices"] = {}
                        latest_gateway_status["ac_devices"][aid] = {
                            "id": aid,
                            "name": ac.get("name", f"Điều hòa {aid}"),
                            "brand": ac.get("brand", 0),
                            "power": ac.get("power", 0),
                            "temp": ac.get("temp", 25),
                            "mode": ac.get("mode", 2),
                            "fan": ac.get("fan", 0),
                            "online": True
                        }

            elif evt == "ac_state":
                aid = str(data.get("id", ""))
                if not aid:
                    return
                if _ac_inventory_ids is not None:
                    _ac_inventory_ids.add(aid)

                latest_gateway_status["online"] = True
                latest_gateway_status["last_seen"] = now.isoformat()

                if "ac_devices" not in latest_gateway_status:
                    latest_gateway_status["ac_devices"] = {}
                dev_entry = latest_gateway_status["ac_devices"].get(aid, {
                    "id": aid,
                    "name": data.get("name", f"Điều hòa {aid}"),
                    "brand": data.get("brand", 0),
                    "power": 0,
                    "temp": 25,
                    "mode": 1,
                    "fan": 0,
                    "online": True
                })
                if "brand" in data and data["brand"]: dev_entry["brand"] = data["brand"]
                if "name" in data and data["name"]: dev_entry["name"] = data["name"]
                if "power" in data: dev_entry["power"] = data["power"]
                if "temp" in data and data["temp"] > 0: dev_entry["temp"] = data["temp"]
                if "mode" in data: dev_entry["mode"] = data["mode"]
                if "fan" in data: dev_entry["fan"] = data["fan"]
                if "brand" in data: dev_entry["brand"] = data["brand"]
                dev_entry["online"] = True
                latest_gateway_status["ac_devices"][aid] = dev_entry

                # Cập nhật thêm vào active ac_id nếu trùng hoặc nếu chưa có active ID
                if not latest_gateway_status.get("ac_id") or latest_gateway_status.get("ac_id") == aid:
                    latest_gateway_status["ac_id"] = aid
                    latest_gateway_status["ac_name"] = dev_entry["name"]
                    latest_gateway_status["ac_power"] = dev_entry["power"]
                    latest_gateway_status["ac_temp"] = dev_entry["temp"]
                    latest_gateway_status["ac_mode"] = dev_entry["mode"]
                    latest_gateway_status["ac_fan"] = dev_entry["fan"]
                    latest_gateway_status["ac_brand"] = dev_entry["brand"]

                logger.info(f"✅ [AC STATE SYNC] Điều hòa [{aid}] '{dev_entry.get('name')}': Power={dev_entry.get('power')}, Temp={dev_entry.get('temp')}°C, Mode={dev_entry.get('mode')}, Fan={dev_entry.get('fan')}")

            elif evt in ("ota_progress", "ota_status"):
                latest_gateway_status["online"] = True
                latest_gateway_status["last_seen"] = now.isoformat()
                latest_gateway_status["ota"].update({
                    "status": data.get("status", "in_progress"),
                    "percent": data.get("percent", 0),
                    "bytes": data.get("bytes", 0),
                    "total": data.get("total", 0),
                    "target": data.get("target", "esp32s3_gate"),
                    "error": data.get("error", None),
                    "message": data.get("message", ""),
                    "timestamp": int(now.timestamp())
                })
                logger.info(f"📡 [OTA PROGRESS] {data.get('target', 'esp32s3_gate')} -> {data.get('percent')}% ({data.get('status')})")

        # 2. Nhận dữ liệu cảm biến Node ESP-NOW
        elif topic == settings.TOPIC_GATE_TELEMETRY or "node_id" in data:
            latest_sensor_data.update({
                "node_id": data.get("node_id", 1),
                "val1": data.get("val1", 0.0),
                "val2": data.get("val2", 0.0),
                "battery": data.get("battery", 0.0),
                "presence": data.get("presence"),
                "rssi": data.get("rssi", -100),
                "last_seen": now.isoformat()
            })

            try:
                with Session(engine) as session:
                    rec = SensorRecord(
                        node_id=data.get("node_id", 1),
                        packet_id=data.get("packet_id", 0),
                        mac=data.get("mac", ""),
                        val1=data.get("val1", 0.0),
                        val2=data.get("val2", 0.0),
                        battery=data.get("battery", 0.0),
                        fw=data.get("fw", "1.0.0"),
                        rssi=data.get("rssi", -100),
                        created_at=now
                    )
                    session.add(rec)
                    session.commit()
            except Exception as db_err:
                logger.error(f"Lỗi ghi dữ liệu cảm biến vào DB: {db_err}")

    except Exception as e:
        logger.error(f"Lỗi phân tích bản tin MQTT: {e}")

def publish_command(cmd_dict: dict) -> bool:
    """Gửi bản tin JSON điều khiển tới Gateway qua MQTT topic gate/command"""
    global mqtt_client
    if not mqtt_client:
        return False
    try:
        payload = json.dumps(cmd_dict)
        res = mqtt_client.publish(settings.TOPIC_GATE_COMMAND, payload, qos=1)
        logger.info(f"Đã bắn lệnh MQTT -> [{settings.TOPIC_GATE_COMMAND}]: {payload}")
        return res.rc == mqtt.MQTT_ERR_SUCCESS
    except Exception as e:
        logger.error(f"Lỗi publish MQTT command: {e}")
        return False

def publish_ota_command(cmd_dict: dict) -> bool:
    """Gửi bản tin JSON cập nhật firmware OTA tới Gateway qua TOPIC_GATE_OTA và TOPIC_GATE_COMMAND"""
    global mqtt_client
    if not mqtt_client:
        return False
    try:
        payload = json.dumps(cmd_dict)
        res_ota = mqtt_client.publish(settings.TOPIC_GATE_OTA, payload, qos=1)
        res_cmd = mqtt_client.publish(settings.TOPIC_GATE_COMMAND, payload, qos=1)
        logger.info(f"Đã phát lệnh OTA -> [{settings.TOPIC_GATE_OTA}] & [{settings.TOPIC_GATE_COMMAND}]: {payload}")
        return (res_ota.rc == mqtt.MQTT_ERR_SUCCESS) or (res_cmd.rc == mqtt.MQTT_ERR_SUCCESS)
    except Exception as e:
        logger.error(f"Lỗi publish OTA command: {e}")
        return False

def start_mqtt_client():
    """Khởi động MQTT Client kết nối HiveMQ Cloud TLS trong background thread"""
    global mqtt_client
    parsed = urlparse(settings.MQTT_BROKER_URI)
    host = parsed.hostname or "20476a36ce36478d90de6d5676587638.s1.eu.hivemq.cloud"
    port = parsed.port or 8883

    mqtt_client = mqtt.Client(
        mqtt.CallbackAPIVersion.VERSION2,
        client_id="fastapi_gate_backend_" + str(int(datetime.utcnow().timestamp())),
        protocol=mqtt.MQTTv311
    )
    mqtt_client.username_pw_set(settings.MQTT_USERNAME, settings.MQTT_PASSWORD)

    if parsed.scheme in ["mqtts", "ssl"] or port == 8883:
        mqtt_client.tls_set(cert_reqs=ssl.CERT_NONE)
        mqtt_client.tls_insecure_set(True)

    mqtt_client.on_connect = on_connect
    mqtt_client.on_disconnect = on_disconnect
    mqtt_client.on_message = on_message

    logger.info(f"Đang kết nối MQTT tới {host}:{port}...")
    try:
        mqtt_client.connect_async(host, port, keepalive=60)
        mqtt_client.loop_start()
    except Exception as e:
        logger.error(f"Không thể kết nối MQTT: {e}")

def stop_mqtt_client():
    global mqtt_client
    if mqtt_client:
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
        logger.info("Đã đóng kết nối MQTT Client.")
