## 🔥 Mục tiêu
Hệ thống phát hiện khói/nồng độ khí nguy hiểm và nhiệt cao:
- Đọc: **DHT11** (nhiệt độ & độ ẩm) và **MQ-2** (khói/gas)
- Hành động: **bật quạt**, **mở cửa tự động** (2 relay), **còi** báo
- Ghi **log** và **status realtime** lên **Firebase Realtime Database**

## 🛠 Phần cứng cần có
- ESP32 (DevKit)
- Cảm biến DHT11 (3 chân: VCC, GND, DATA)
- MQ-2 gas/smoke module (có breakout board, tín hiệu analog)
- 2 x Relay module (1 cho quạt, 1 cho cửa)
- Quạt (12V hoặc 5V tuỳ bạn, qua relay)
- Cửa tự động (mô tơ, cơ cấu) hoặc mô phỏng với relay
- Buzzer piezo (optional)
- Dây nối, nguồn phù hợp

## 🔌 Sơ đồ nối chân
| Module | Chân | ESP32 |
|--------|------|-------|
| DHT11  | DATA | GPIO4 |
| DHT11  | VCC  | 3.3V  |
| DHT11  | GND  | GND   |
| MQ-2   | AOUT | GPIO34 (ADC1_6) |
| Relay (Fan) | IN | GPIO16 |
| Relay (Door)| IN | GPIO17 |
| Buzzer | IN | GPIO2 |
| Relay VCC | VCC | 5V (kiểm tra relay module) |
| Relay GND | GND | GND |

> **Lưu ý:** Nếu relay module cần 5V VCC, đảm bảo dùng chung GND giữa ESP32 và nguồn 5V.

## 📦 Phần mềm & Thư viện
- Arduino IDE
- Board support for ESP32
- Thư viện: `DHT sensor library` (Adafruit)
- Thư viện `HTTPClient` (có sẵn trong ESP32 core)

🔁 Cơ chế hoạt động (tóm tắt)
- ESP32 kết nối WiFi.
- Đọc DHT11 (nhiệt, ẩm) và MQ-2 (analog) mỗi 5s.
- Nếu MQ2_raw >= MQ2_SMOKE_THRESHOLD hoặc temperature >= TEMP_FIRE_THRESHOLD → kích hoạt alarm:
- Bật relay quạt (FAN)
- Mở cửa (DOOR relay)
- Bật còi (buzzer)
- Ghi log (POST /logs) và cập nhật status (PUT /status) lên Firebase.
- Ghi sự kiện cuối cùng vào /last_alarm.

🔧 Điều chỉnh & hiệu chuẩn
- MQ-2: cần calibrate để biết ngưỡng chính xác (thử nghiệm thực tế trong môi trường).
- MQ2_SMOKE_THRESHOLD mặc định là 300 (ADC 0-4095). Thay theo module và nguồn.
- TEMP_FIRE_THRESHOLD mặc định 55°C — điều chỉnh theo yêu cầu.
- READ_INTERVAL để điều chỉnh tần suất đọc/gửi dữ liệu.

🔐 Bảo mật & production
-Không để FIREBASE_AUTH công khai trong repo. Dùng biến môi trường hoặc secret manager.
-Thiết lập rules của Firebase Realtime Database hợp lý (không để mở public lâu dài).
-Với hệ thống thực tế, cân nhắc thêm: SSL, xác thực thiết bị, ghi log an toàn, backup.
