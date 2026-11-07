# Hunonic Bridge: lỗi đã gặp và cách khắc phục

Tài liệu này tổng hợp quá trình tích hợp Hunonic Cloud cho Gateway ESP32-S3.
Không ghi token, mật khẩu, topic đầy đủ hoặc thông tin Wi-Fi vào tài liệu.

## 1. API đồng bộ thiết bị trả HTTP 404

### Dấu hiệu

```text
Sync thiet bi that bai: err=0, HTTP status=404
Phan hoi tu server: 404 page not found
```

### Nguyên nhân

Firmware dùng endpoint cũ `listDeviceByHome`. API Hunonic hiện yêu cầu lấy danh sách nhà trước, sau đó truyền `home_id` khi lấy thiết bị.

### Cách sửa

Luồng đồng bộ được đổi thành:

1. `GET /hun-api/home/list-home`
2. Lấy `data[0].id` làm `home_id`.
3. `GET /hun-api/device/listDeviceOfHomeSelect?home_id=<home_id>`.

File xử lý: `firmware/GATE/middle/hunonic_bridge/hunonic_bridge.c`.

---

## 2. WebSocket trả 401 hoặc không bắt tay được

### Dấu hiệu

```text
transport_ws: Sec-WebSocket-Accept not found
esp_ws_handshake_status_code=401
```

### Nguyên nhân

Token lưu trong NVS đã hết hạn hoặc đã bị một phiên đăng nhập khác thay thế.

### Cách sửa

- Chỉ mở WebSocket sau khi HTTP sync thành công.
- Khi API trả `401`, `403` hoặc `500`, Gateway tự login một lần bằng thông tin cấu hình rồi sync lại.
- Không khởi động thêm WebSocket trong lúc re-login; dùng lại luồng khởi tạo bình thường.

Lưu ý: Hunonic có thể giới hạn một phiên đăng nhập. Khi Gateway login lấy token mới, app/web đang dùng cùng tài khoản có thể bị đăng xuất và ngược lại.

---

## 3. Một IR Hub bị hiển thị thành một điều hòa, Hub báo offline

### Dấu hiệu

```text
-> Phat hien Dieu hoa: [3987670] 'IR trung tâm'
Dong bo hoan tat: tim thay 2 dieu hoa
Khong tim thay Tram IR Hub trong tai khoan Hunonic -> OFFLINE
```

Trong thực tế chỉ có một điều hòa và một IR Hub.

### Nguyên nhân

Parser coi thiết bị có `root_type=irwifi/irwifiv2` là AC. IR Hub cũng có `root_id`, nên bị lọt vào nhánh nhận diện thiết bị con.

### Cách sửa

- `type=irwifi` hoặc `type=irwifiv2` luôn được xác định là IR Hub.
- IR Hub bị loại khỏi danh sách AC.
- Hub ID lấy ưu tiên từ `dev.id` số, ví dụ `3987670`.
- AC chỉ nhận diện qua `irchild`, category AC, hoặc metadata thiết bị con của Hub.
- Loại bỏ ID Hub hard-code.

Kết quả mong đợi:

```text
Hunonic API: Tram IR Hub [3987670]: ONLINE
-> Phat hien Dieu hoa: [4062921] '...'
Dong bo hoan tat: tim thay 1 dieu hoa
```

---

## 4. Subscribe topic lặp nhiều lần

### Dấu hiệu

Sau mỗi chu kỳ sync xuất hiện lại nhiều dòng:

```text
=== Bat dau Subscribe cac topic Hunonic Cloud ===
-> [WS SUBSCRIBE] Topic: ...
```

### Nguyên nhân

- Cùng một thiết bị có thể được API trả về nhiều lần theo room/dữ liệu.
- Firmware subscribe lại toàn bộ topic sau mỗi lần sync.

### Cách sửa

- Khử trùng lặp AC theo `id` khi parse.
- Lưu cache topic đã subscribe trong phiên WebSocket.
- Chỉ gửi subscribe thực sự khi topic chưa có trong cache.
- Xóa cache topic khi WebSocket disconnect để lần kết nối mới subscribe lại đầy đủ.

---

## 5. Thêm AC mới nhưng lệnh rơi vào máy lạnh đầu tiên

### Dấu hiệu

Thiết bị mới thêm trong app chưa nằm trong cache Gateway nhưng lệnh vẫn được gửi sang một AC khác.

### Nguyên nhân

Code cũ fallback sang `s_ac_devices[0]` khi không tìm thấy ID được yêu cầu.

### Cách sửa

- Khi lệnh có ID nhưng ID chưa có trong cache: gọi sync ngay, tìm lại đúng ID rồi mới gửi.
- Nếu sau sync vẫn không có ID: trả `ESP_ERR_NOT_FOUND`.
- Chỉ được fallback AC đầu tiên nếu caller cố ý không truyền ID.
- Tăng giới hạn `MAX_HUNONIC_AC_DEVICES` từ 4 lên 16.
- Lưu Hub cha cho mỗi AC để hỗ trợ nhiều IR Hub trong tương lai.

---

## 6. App Hunonic điều khiển được nhưng Gateway không điều khiển được

### Dấu hiệu

Gateway log có:

```text
-> BAN LENH QUA WS: Dieu hoa [4062921] ...
```

Nhưng không có phản hồi `AC_STATE`; điều hòa không đổi trạng thái. Khi bấm từ app chính chủ thì có hai frame `AC_STATE` trả về.

### Nguyên nhân

Payload lệnh ban đầu được suy đoán sai, cụ thể từng dùng:

- event `IR_CUSTOM_REMOTE_CONTROL` thay vì event thực tế;
- `root_id` API token thô hoặc ID Hub chưa được mã hóa đúng;
- kiểu dữ liệu của `u` và các trường action khác không khớp client chính chủ.

### Cách xác minh

Đã mở web Hunonic trong trình duyệt riêng, bắt WebSocket frame khi bấm điều khiển và giải mã AES-GCM. Frame hợp lệ có cấu trúc rút gọn:

```json
{
  "topic": "<topicSub>",
  "type": "pub",
  "payload": {
    "id": "<ac_id>",
    "type": "irwifiv2",
    "root_type": "irwifiv2",
    "event": "AC_CONTROL",
    "root_id": "<Hub ID da ma hoa moi>",
    "action": {
      "irwifiv2": 1,
      "type": 1,
      "brand": 104,
      "power": 0,
      "temp": 30,
      "mode": 1,
      "fan": 0,
      "act": 0,
      "u": "<user_id>"
    }
  }
}
```

### Cách sửa

- Dùng event `AC_CONTROL`.
- Mã hóa lại Hub ID số cho mỗi lệnh bằng `hunonic_crypto_encrypt()`.
- Giữ `u` là chuỗi.
- Không tự thêm `swingv`/`swingh` khi frame chính chủ của model không có hai trường này.
- `root_id` token API có thể dài; struct đã tăng buffer lên 256 byte để không cắt dữ liệu khi cần lưu metadata.

Sau khi sửa, dấu hiệu thành công là:

```text
Gui AC_CONTROL: AC=..., IR Hub=..., brand=...
-> BAN LENH QUA WS: ...
<- [WS INCOMING DECRYPTED]: ... "event":"AC_STATE" ...
```

---

## 7. Bật được nhưng lệnh tắt đôi lúc không gửi được: `esp-aes: Failed to allocate memory`

### Dấu hiệu

```text
Dang lay danh sach nha Hunonic ...
esp-aes: Failed to allocate memory
HUNONIC_CRYPTO: mbedtls_gcm_crypt_and_tag that bai: -1
Ma hoa lenh dieu khien that bai!
```

Có thể đi kèm lỗi TLS:

```text
esp-sha: Failed to allocate buf memory
esp-tls-mbedtls: mbedtls_ssl_handshake returned ...
```

### Nguyên nhân

Periodic sync chạy HTTP/TLS cùng lúc với task xử lý lệnh MQTT. TLS và AES-GCM đều cần heap mbedTLS; khi trùng thời điểm, cấp phát cho AES thất bại nên lệnh bị mất.

### Cách sửa

- Thêm mutex I/O Hunonic (`s_hunonic_io_mutex`).
- `hunonic_bridge_sync_devices()` giữ mutex trong thời gian HTTP/TLS, parse và subscribe.
- `hunonic_bridge_control()` chờ mutex trước khi mã hóa/gửi WebSocket.
- Lệnh không bị bỏ: nếu sync đang chạy, nó chờ sync xong rồi mới gửi.

### Kiểm tra sau khi flash

Thử bấm bật/tắt khi log đang có `Dang lay danh sach nha Hunonic...`. Không được còn xuất hiện:

```text
esp-aes: Failed to allocate memory
Ma hoa lenh dieu khien that bai!
```

---

## 8. Lỗi certificate/TLS rời rạc

### Dấu hiệu

```text
Certificate validated
PK verify failed ...
Failed to verify peer certificate
HTTP status=0
```

### Nguyên nhân thường gặp

- NTP/chứng chỉ đang cập nhật ngay sau Wi-Fi reconnect.
- Nhiều kết nối TLS đồng thời gây thiếu heap.
- Kết nối mạng không ổn định.

### Hướng xử lý hiện có

- Sync định kỳ có cơ chế thử lại ở chu kỳ sau.
- Mutex I/O Hunonic giảm đáng kể lỗi thiếu heap do TLS và AES chạy đồng thời.
- Không đánh dấu Gateway offline chỉ vì một lần sync thất bại; WebSocket/MQTT có thể vẫn hoạt động.

## File chính đã thay đổi

- `firmware/GATE/middle/hunonic_bridge/hunonic_bridge.c`
- `firmware/GATE/middle/hunonic_bridge/hunonic_bridge.h`
- `firmware/GATE/middle/hunonic_bridge/hunonic_crypto.c`
- `firmware/GATE/middle/hunonic_bridge/hunonic_crypto.h`

## Quy trình kiểm tra ngắn

1. Flash firmware Gateway mới.
2. Chờ log báo Hub online và tìm thấy đúng số AC.
3. Bật/tắt một AC từ web nội bộ.
4. Xác nhận Gateway có `AC_CONTROL` và server trả `AC_STATE`.
5. Thử gửi lệnh đúng lúc periodic sync đang chạy để xác nhận không còn lỗi cấp phát AES.

## 10. Thiết bị đã xóa trên web Hunonic vẫn xuất hiện ở Gateway

Đã đối chiếu JavaScript công khai của `https://web.hunonic.com/` (entry `index-CKhmLENm.js`): khi force-refresh, web gọi `listDeviceOfHomeSelect` với `home_id` và `_ts`; xác nhận `status === true`, tìm đúng home trong `data`, kiểm tra `rooms[].devices`, sau đó thay thế snapshot cũ.

Firmware được cập nhật tương ứng:

- Thêm `_ts` và header `Cache-Control: no-cache, no-store`, `Pragma: no-cache` cho yêu cầu đọc.
- Giữ home đã chọn ổn định nếu API đổi thứ tự; chỉ đọc thiết bị của đúng home đó.
- Kiểm tra toàn bộ snapshot trước khi thay danh sách. Lỗi API, thiếu home hoặc cấu trúc không hợp lệ không được biến thành danh sách rỗng thành công.
- Snapshot hợp lệ thay toàn bộ danh sách, kể cả khi không còn thiết bị; ID biến mất được log `[SYNC REMOVED]`.
- Phát callback trạng thái sau khi đã dựng xong danh sách, tránh callback nhìn thấy danh sách dở dang.
- Sự kiện WebSocket chứa ID cụ thể đã bị xóa không được ghép theo brand sang AC khác.
- Không publish `device_list` như một bản đồng bộ mới nếu HTTP sync thất bại.
- Backend loại ID ngoài inventory đã xác nhận; frontend dựng lại children theo API thay vì nối thêm hoặc dùng MIRAGE/FUNIKI mẫu.

Đã build firmware và kiểm tra host cho chọn đúng home, snapshot rỗng hợp lệ, API lỗi, thiếu rooms/devices; kiểm tra backend cho xóa ID cũ và bỏ qua sự kiện trạng thái đến muộn.

Cần nạp lại Gate để kiểm chứng với tài khoản thực. Nếu yêu cầu bỏ cache vẫn trả hai ID, không tự ép `ac_count=1`: đối chiếu user/home với web Hunonic đang mở và response API mới. Chưa xác nhận nguyên nhân phía cloud khi chưa chạy lại phần cứng.
