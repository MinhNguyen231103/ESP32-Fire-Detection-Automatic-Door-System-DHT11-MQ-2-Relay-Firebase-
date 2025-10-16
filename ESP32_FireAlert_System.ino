#include <WiFi.h>
#include <HTTPClient.h>
#include "DHT.h"

// --------- Cấu hình cảm biến & chân ---------
#define DHTPIN 4            // DATA DHT11 -> GPIO4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define MQ2_PIN 34          // Chân analog đọc MQ-2 (ADC1_6)
#define RELAY_FAN 16        // Relay điều khiển quạt
#define RELAY_DOOR 17       // Relay điều khiển cửa
#define BUZZER_PIN 2        // Buzzer (optional)

// --------- Cấu hình WiFi & Firebase ---------
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Firebase Realtime Database URL
const char* FIREBASE_HOST = "https://YOUR_FIREBASE_DB_URL";

// Nếu bạn sử dụng Database secret hoặc auth token, đặt vào đây, nếu không để "" (không an toàn cho production)
const char* FIREBASE_AUTH = ""; // ví dụ: "abcd1234:XYZ"

// --------- Hệ số & ngưỡng (có thể chỉnh) ---------
const int MQ2_SMOKE_THRESHOLD = 300;   // giá trị ADC (0-4095): > threshold => cảnh báo khói (tùy module MQ2, cần calibrate)
const float TEMP_FIRE_THRESHOLD = 55.0; // Nếu nhiệt độ > threshold (°C) có thể coi nguy cơ cháy cao
const unsigned long READ_INTERVAL = 5000; // đọc cảm biến & gửi dữ liệu mỗi 5s
const unsigned long FIRE_ACTION_MIN_DURATION = 30000; // hành động khi phát hiện cháy ít nhất 30s

// --------- Biến trạng thái ---------
unsigned long lastRead = 0;
bool alarmState = false;
unsigned long alarmStartMillis = 0;

// --------- Hàm tiện ích HTTP -> Firebase ---------
String firebasePathAuth(const char* path) {
  String url = String(FIREBASE_HOST) + String(path) + ".json";
  if (strlen(FIREBASE_AUTH) > 0) {
    url += "?auth=";
    url += FIREBASE_AUTH;
  }
  return url;
}

bool firebasePUT(const String &path, const String &json) {
  HTTPClient http;
  String url = firebasePathAuth(path.c_str());
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(json);
  http.end();
  return (code >= 200 && code < 300);
}

bool firebasePOST(const String &path, const String &json) {
  HTTPClient http;
  String url = firebasePathAuth(path.c_str());
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(json);
  http.end();
  return (code >= 200 && code < 300);
}

// --------- Setup & Loop ---------
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(MQ2_PIN, INPUT);
  pinMode(RELAY_FAN, OUTPUT);
  pinMode(RELAY_DOOR, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Tắt relay/buzzer lúc khởi động
  digitalWrite(RELAY_FAN, LOW);
  digitalWrite(RELAY_DOOR, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();

  Serial.println();
  Serial.printf("Connecting to WiFi '%s' ...\n", ssid);
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > 20000) { // 20s timeout
      Serial.println("\nKhông thể kết nối WiFi trong 20s. Thử lại...");
      start = millis();
    }
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Ghi trạng thái ban đầu lên Firebase
  sendStatusToFirebase("boot", "system_start");
}

void loop() {
  unsigned long now = millis();
  if (now - lastRead >= READ_INTERVAL) {
    lastRead = now;
    readAndProcessSensors();
  }

  // Nếu đang cảnh báo, đảm bảo giữ các hành động trong tối thiểu FIRE_ACTION_MIN_DURATION
  if (alarmState && (millis() - alarmStartMillis < FIRE_ACTION_MIN_DURATION)) {
    // vẫn giữ trạng thái hành động
    // (nhiệm vụ chính đã làm trong readAndProcessSensors)
  } else {
    // nothing extra for now
  }
}

// --------- Hàm đọc & xử lý cảm biến ---------
void readAndProcessSensors() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature(); // °C
  int mq2_raw = analogRead(MQ2_PIN); // 0-4095

  // Kiểm tra đọc DHT
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Lỗi đọc DHT11!");
    return;
  }

  bool smokeDetected = (mq2_raw >= MQ2_SMOKE_THRESHOLD);
  bool tempHigh = (temperature >= TEMP_FIRE_THRESHOLD);
  bool newAlarm = smokeDetected || tempHigh;

  Serial.println("----- Sensor Read -----");
  Serial.printf("Temperature: %.1f C\n", temperature);
  Serial.printf("Humidity: %.1f %%\n", humidity);
  Serial.printf("MQ2 raw: %d %s\n", mq2_raw, smokeDetected ? "(SMOKE!)" : "");
  Serial.printf("Alarm? %s\n", newAlarm ? "YES" : "no");

  if (newAlarm && !alarmState) {
    // Bắt đầu cảnh báo
    alarmState = true;
    alarmStartMillis = millis();
    triggerAlarmActions(true);
    sendAlarmToFirebase(true, temperature, humidity, mq2_raw);
  } else if (!newAlarm && alarmState) {
    // Nếu hết điều kiện cảnh báo và đã qua thời gian tối thiểu thì tắt
    if (millis() - alarmStartMillis >= FIRE_ACTION_MIN_DURATION) {
      alarmState = false;
      triggerAlarmActions(false);
      sendAlarmToFirebase(false, temperature, humidity, mq2_raw);
    } else {
      // vẫn đang trong khoảng giữ trạng thái; gửi status vẫn báo động
      sendLogToFirebase(temperature, humidity, mq2_raw, true);
    }
  } else {
    // Trạng thái không thay đổi: gửi log thông thường
    sendLogToFirebase(temperature, humidity, mq2_raw, alarmState);
    // cập nhật status realtime
    sendStatusRealtime(temperature, humidity, mq2_raw, alarmState);
  }
}

// --------- Hành động khi cảnh báo (bật/quay quạt, mở cửa, còi) ---------
void triggerAlarmActions(bool on) {
  if (on) {
    digitalWrite(RELAY_FAN, HIGH);   // bật quạt
    digitalWrite(RELAY_DOOR, HIGH);  // mở cửa (tùy cơ cấu relay)
    // Buzzer nháy
    for (int i=0;i<3;i++){
      digitalWrite(BUZZER_PIN, HIGH);
      delay(200);
      digitalWrite(BUZZER_PIN, LOW);
      delay(200);
    }
    Serial.println(">>> ACTIONS: FAN ON, DOOR OPEN, BUZZER");
  } else {
    digitalWrite(RELAY_FAN, LOW);
    digitalWrite(RELAY_DOOR, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println(">>> ACTIONS: FAN OFF, DOOR CLOSED, BUZZER OFF");
  }
}

// --------- Gửi log (POST) ---------
void sendLogToFirebase(float t, float h, int mq2, bool alarm) {
  // Log object
  String payload = "{";
  payload += "\"temperature\":" + String(t,1) + ",";
  payload += "\"humidity\":" + String(h,1) + ",";
  payload += "\"mq2_raw\":" + String(mq2) + ",";
  payload += "\"alarm\":" + String(alarm ? "true" : "false") + ",";
  payload += "\"uptime_ms\":" + String(millis());
  payload += "}";

  bool ok = firebasePOST("/logs", payload);
  if (!ok) {
    Serial.println("Gửi log lên Firebase thất bại");
  } else {
    Serial.println("Đã gửi log lên Firebase");
  }
}

// --------- Gửi trạng thái realtime (PUT so that /status always current) ---------
void sendStatusRealtime(float t, float h, int mq2, bool alarm) {
  String payload = "{";
  payload += "\"temperature\":" + String(t,1) + ",";
  payload += "\"humidity\":" + String(h,1) + ",";
  payload += "\"mq2_raw\":" + String(mq2) + ",";
  payload += "\"alarm\":" + String(alarm ? "true" : "false") + ",";
  payload += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  payload += "}";

  bool ok = firebasePUT("/status", payload);
  if (!ok) {
    Serial.println("Ghi status realtime thất bại");
  } else {
    Serial.println("Đã update /status trên Firebase");
  }
}

// --------- Gửi event alarm (PUT vào /last_alarm hoặc cập nhật trường riêng) ---------
void sendAlarmToFirebase(bool alarmOn, float t, float h, int mq2) {
  String payload = "{";
  payload += "\"alarm\":" + String(alarmOn ? "true" : "false") + ",";
  payload += "\"temperature\":" + String(t,1) + ",";
  payload += "\"humidity\":" + String(h,1) + ",";
  payload += "\"mq2_raw\":" + String(mq2) + ",";
  payload += "\"timestamp_ms\":" + String(millis());
  payload += "}";
  bool ok = firebasePUT("/last_alarm", payload);
  if (!ok) {
    Serial.println("Ghi last_alarm lên Firebase thất bại");
  } else {
    Serial.println("Đã cập nhật last_alarm trên Firebase");
  }
}

void sendStatusToFirebase(const char* key, const char* value) {
  String payload = "{";
  payload += "\"" + String(key) + "\":\"" + String(value) + "\"";
  payload += "}";
  firebasePUT("/meta", payload);
}

