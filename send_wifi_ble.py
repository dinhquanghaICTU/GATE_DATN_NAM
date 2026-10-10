#!/usr/bin/env python3
"""
Gửi cấu hình Wi-Fi vào ESP32-S3 Gateway qua BLE bằng Linux BlueZ D-Bus.
Hoàn toàn độc lập với Chrome, không cần cấp quyền Web Bluetooth trình duyệt.
"""

import sys
import time
import dbus
import dbus.mainloop.glib
from gi.repository import GLib

GATEWAY_NAME_PREFIX = "GATE_S3_CONFIG"
SERVICE_UUID = "000000ff-0000-1000-8000-00805f9b34fb"
CHAR_SSID_UUID = "0000ff01-0000-1000-8000-00805f9b34fb"
CHAR_PASS_UUID = "0000ff02-0000-1000-8000-00805f9b34fb"

def send_wifi(ssid, password):
    print(f"[*] Đang chuẩn bị gửi Wi-Fi qua BLE: SSID='{ssid}', PASS='{password}'")
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    manager = dbus.Interface(bus.get_object("org.bluez", "/"), "org.freedesktop.DBus.ObjectManager")
    objects = manager.GetManagedObjects()

    # Tìm adapter
    adapter_path = None
    for path, interfaces in objects.items():
        if "org.bluez.Adapter1" in interfaces:
            adapter_path = path
            break

    if not adapter_path:
        print("[!] Không tìm thấy Bluetooth adapter trên máy.")
        return False

    adapter = dbus.Interface(bus.get_object("org.bluez", adapter_path), "org.bluez.Adapter1")

    # Tìm thiết bị Gateway
    device_path = None
    for path, interfaces in objects.items():
        if "org.bluez.Device1" in interfaces:
            dev = interfaces["org.bluez.Device1"]
            name = dev.get("Name", "")
            if name.startswith(GATEWAY_NAME_PREFIX) or "GATE" in name:
                device_path = path
                print(f"[+] Tìm thấy Gateway đã phát hiện trước đó: {name} ({path})")
                break

    if not device_path:
        print("[*] Đang bật quét Bluetooth tìm GATE_S3_CONFIG (3 giây)...")
        try:
            adapter.StartDiscovery()
            time.sleep(3.5)
            adapter.StopDiscovery()
        except Exception as e:
            pass

        objects = manager.GetManagedObjects()
        for path, interfaces in objects.items():
            if "org.bluez.Device1" in interfaces:
                dev = interfaces["org.bluez.Device1"]
                name = dev.get("Name", "")
                if name.startswith(GATEWAY_NAME_PREFIX) or "GATE" in name:
                    device_path = path
                    print(f"[+] Đã tìm thấy Gateway: {name} ({path})")
                    break

    if not device_path:
        print("[!] Không tìm thấy thiết bị GATE_S3_CONFIG trong tầm phát BLE!")
        print("    Vui lòng kiểm tra ESP32-S3 đã bật nguồn và đang ở chế độ BLE Config (LED xanh dương).")
        return False

    # Kết nối thiết bị
    dev_obj = bus.get_object("org.bluez", device_path)
    device = dbus.Interface(dev_obj, "org.bluez.Device1")
    dev_props = dbus.Interface(dev_obj, "org.freedesktop.DBus.Properties")

    connected = dev_props.Get("org.bluez.Device1", "Connected")
    if not connected:
        print(f"[*] Đang kết nối tới {device_path}...")
        try:
            device.Connect()
            time.sleep(1.5)
        except Exception as e:
            print(f"[!] Lỗi kết nối: {e}")
            return False

    # Chờ ServicesResolved
    for _ in range(15):
        objects = manager.GetManagedObjects()
        resolved = False
        if device_path in objects and "org.bluez.Device1" in objects[device_path]:
            resolved = objects[device_path]["org.bluez.Device1"].get("ServicesResolved", False)
        if resolved:
            break
        time.sleep(0.3)

    # Tìm characteristics
    char_ssid_path = None
    char_pass_path = None
    objects = manager.GetManagedObjects()
    for path, interfaces in objects.items():
        if "org.bluez.GattCharacteristic1" in interfaces and path.startswith(device_path):
            uuid = interfaces["org.bluez.GattCharacteristic1"].get("UUID", "").lower()
            if uuid == CHAR_SSID_UUID or uuid.startswith("0000ff01"):
                char_ssid_path = path
            elif uuid == CHAR_PASS_UUID or uuid.startswith("0000ff02"):
                char_pass_path = path

    if not char_ssid_path or not char_pass_path:
        print("[!] Không tìm thấy GATT characteristics 0xFF01 hoặc 0xFF02 trên Gateway!")
        return False

    # Ghi SSID
    char_ssid = dbus.Interface(bus.get_object("org.bluez", char_ssid_path), "org.bluez.GattCharacteristic1")
    ssid_bytes = [dbus.Byte(b) for b in ssid.encode("utf-8")]
    print(f"[*] Đang ghi SSID: '{ssid}' vào Characteristic 0xFF01...")
    char_ssid.WriteValue(ssid_bytes, {})
    time.sleep(0.3)
    print("[+] Ghi SSID thành công!")

    # Ghi Password
    char_pass = dbus.Interface(bus.get_object("org.bluez", char_pass_path), "org.bluez.GattCharacteristic1")
    pass_bytes = [dbus.Byte(b) for b in password.encode("utf-8")]
    print(f"[*] Đang ghi Password: '{password}' vào Characteristic 0xFF02...")
    char_pass.WriteValue(pass_bytes, {})
    time.sleep(0.3)
    print("[+] Ghi Password thành công!")

    print("\n🎉 HOÀN THÀNH: Đã nạp thông tin Wi-Fi vào Gateway ESP32-S3 qua BLE thành công!")
    print("   Gateway đang bắt đầu kết nối Wi-Fi & MQTT Cloud HiveMQ.")
    return True

if __name__ == "__main__":
    ssid_arg = sys.argv[1] if len(sys.argv) > 1 else "QuangHa"
    pass_arg = sys.argv[2] if len(sys.argv) > 2 else "66668888"
    success = send_wifi(ssid_arg, pass_arg)
    sys.exit(0 if success else 1)
