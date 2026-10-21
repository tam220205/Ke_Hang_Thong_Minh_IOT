#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "HX711.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ==========================================
// 1. CẤU HÌNH PHẦN CỨNG & MÀN HÌNH OLED
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==========================================
// 2. CẤU HÌNH MẠNG WIFI & MQTT BROKER
// ==========================================
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// Sử dụng HiveMQ Public Broker miễn phí, ổn định cao cho mô phỏng & mạch thật
const char* mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 1883;

// Các MQTT Topics theo chuẩn quy ước Tầng 2
const char* TOPIC_TELEMETRY = "nhom22/smartshelf/telemetry"; // ESP32 -> Broker -> Node-RED
const char* TOPIC_CMD       = "nhom22/smartshelf/cmd";       // Node-RED / Dashboard -> ESP32

WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences prefs;

// ==========================================
// 3. CẤU HÌNH TELEGRAM BOT TRỰC TIẾP TỪ ESP32
// ==========================================
const char* BOT_TOKEN = "8838533602:AAERwan30IWcSVd7wOiZlTo8A68JZ1M2hM0";
const char* CHAT_ID   = "8349343826";

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

unsigned long lastTelegramCheck = 0;
const unsigned long telegramCheckInterval = 3500; // Kiểm tra tin nhắn Telegram mỗi 3.5 giây

// ==========================================
// 4. ĐỊNH NGHĨA CẤU TRÚC KỆ HÀNG (3 KỆ)
// ==========================================
struct Shelf {
  const char* id;          // "K1", "K2", "K3"
  String name;             // Tên sản phẩm
  float unit_weight;       // Khối lượng 1 đơn vị (gram)
  int min_qty;             // Ngưỡng báo động số lượng
  int dout_pin;
  int sck_pin;
  int led_pin;
  HX711 scale;
  float current_weight;    // gram
  int current_qty;
  bool is_low_stock;
  bool last_alert_sent;    // Đã gửi thông báo cảnh báo qua Telegram chưa
  unsigned long last_alert_time; // Thời điểm gửi thông báo gần nhất (ms)
};

// 3 Kệ hàng quản lý bởi ESP32
Shelf shelves[3] = {
  {"K1", "Coca",      330.0, 3, 19, 18, 2,  HX711(), 0.0, 0, false, false, 0},
  {"K2", "BanhOishi",  50.0, 5,  5, 17, 4,  HX711(), 0.0, 0, false, false, 0},
  {"K3", "MiHaoHao",   75.0, 4, 25, 26, 27, HX711(), 0.0, 0, false, false, 0}
};

// Chu kỳ thời gian
unsigned long lastTelemetryTime = 0;
const unsigned long telemetryInterval = 2500; // Gửi dữ liệu MQTT mỗi 2.5 giây

unsigned long lastMqttReconnectAttempt = 0;
const unsigned long mqttReconnectInterval = 5000; // Thử kết nối lại MQTT mỗi 5 giây

// ==========================================
// 5. LƯU & ĐỌC CẤU HÌNH TỪ BỘ NHỚ FLASH (PREFERENCES)
// ==========================================
void loadPreferences() {
  prefs.begin("smartshelf", true);
  for (int i = 0; i < 3; i++) {
    String keyName = String(shelves[i].id) + "_name";
    String keyUnit = String(shelves[i].id) + "_unit";
    String keyMin  = String(shelves[i].id) + "_min";

    if (prefs.isKey(keyName.c_str())) shelves[i].name = prefs.getString(keyName.c_str(), shelves[i].name);
    if (prefs.isKey(keyUnit.c_str())) shelves[i].unit_weight = prefs.getFloat(keyUnit.c_str(), shelves[i].unit_weight);
    if (prefs.isKey(keyMin.c_str()))  shelves[i].min_qty = prefs.getInt(keyMin.c_str(), shelves[i].min_qty);
  }
  prefs.end();
}

void saveShelfConfig(int index) {
  prefs.begin("smartshelf", false);
  String keyName = String(shelves[index].id) + "_name";
  String keyUnit = String(shelves[index].id) + "_unit";
  String keyMin  = String(shelves[index].id) + "_min";

  prefs.putString(keyName.c_str(), shelves[index].name);
  prefs.putFloat(keyUnit.c_str(), shelves[index].unit_weight);
  prefs.putInt(keyMin.c_str(), shelves[index].min_qty);
  prefs.end();
}

// ==========================================
// 6. CẬP NHẬT MÀN HÌNH OLED
// ==========================================
void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(WHITE);

  // Dòng 1: Tiêu đề + Trạng thái kết nối MQTT
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("SMART SHELF");
  display.setCursor(76, 0);
  if (mqttClient.connected()) {
    display.print("[MQTT:OK]");
  } else {
    display.print("[MQTT:NO]");
  }
  display.drawLine(0, 10, 127, 10, WHITE);

  // Dòng 2-4: Thông tin 3 Kệ hàng
  for (int i = 0; i < 3; i++) {
    int y = 14 + (i * 14);
    display.setCursor(0, y);
    display.printf("%s:%-6.6s", shelves[i].id, shelves[i].name.c_str());

    display.setCursor(68, y);
    if (shelves[i].is_low_stock) {
      display.printf("!%2dd(%dg)", shelves[i].current_qty, (int)shelves[i].current_weight);
    } else {
      display.printf(" %2dd(%dg)", shelves[i].current_qty, (int)shelves[i].current_weight);
    }
  }

  // Chân trang: Đếm số kệ cần nhập hàng
  display.drawLine(0, 54, 127, 54, WHITE);
  display.setCursor(0, 56);
  int alertCount = 0;
  for (int i = 0; i < 3; i++) {
    if (shelves[i].is_low_stock) alertCount++;
  }
  if (alertCount > 0) {
    display.printf("CAN NHAP: %d KE THIEU!", alertCount);
  } else {
    display.print("Trang thai: DU HANG");
  }
  display.display();
}

// ==========================================
// 7. GỬI DỮ LIỆU TELEMETRY QUA MQTT (JSON)
// ==========================================
void publishTelemetry() {
  if (!mqttClient.connected()) return;

  JsonDocument doc;
  doc["device"] = "ESP32_SmartShelf";
  doc["uptime"] = millis() / 1000;

  int totalAlert = 0;
  JsonArray array = doc["shelves"].to<JsonArray>();

  for (int i = 0; i < 3; i++) {
    JsonObject obj = array.add<JsonObject>();
    obj["id"]        = shelves[i].id;
    obj["name"]      = shelves[i].name;
    obj["weight"]    = (int)shelves[i].current_weight;
    obj["unit"]      = (int)shelves[i].unit_weight;
    obj["qty"]       = shelves[i].current_qty;
    obj["min"]       = shelves[i].min_qty;
    obj["low_stock"] = shelves[i].is_low_stock;

    if (shelves[i].is_low_stock) totalAlert++;
  }
  doc["total_alerts"] = totalAlert;

  char buffer[512];
  size_t n = serializeJson(doc, buffer);

  bool success = mqttClient.publish(TOPIC_TELEMETRY, buffer);
  if (success) {
    Serial.printf("[MQTT PUB] -> %s (%d bytes)\n", TOPIC_TELEMETRY, (int)n);
  } else {
    Serial.println("[MQTT PUB FAILED] Không thể gửi gói tin telemetry!");
  }
}

// ==========================================
// 8. TỰ ĐỘNG GỬI CẢNH BÁO TELEGRAM TRỰC TIẾP
// ==========================================
void checkAndSendTelegramAlerts() {
  if (WiFi.status() != WL_CONNECTED) return;
  unsigned long now = millis();

  String alertMsg = "";
  int alertCount = 0;

  for (int i = 0; i < 3; i++) {
    if (shelves[i].is_low_stock) {
      // Gửi ngay khi vừa phát hiện thiếu hàng, hoặc nhắc lại sau mỗi 5 phút (300,000ms)
      if (!shelves[i].last_alert_sent || (now - shelves[i].last_alert_time >= 300000)) {
        shelves[i].last_alert_sent = true;
        shelves[i].last_alert_time = now;
        alertMsg += "⚠️ *KỆ [" + String(shelves[i].id) + "] - " + shelves[i].name + "*: Còn *" + 
                    String(shelves[i].current_qty) + "* món (Tải trọng: " + 
                    String((int)shelves[i].current_weight) + "g, Ngưỡng: " + 
                    String(shelves[i].min_qty) + ")\n";
        alertCount++;
      }
    } else {
      // Nếu trước đó đang cảnh báo mà nay người dùng đã thêm hàng vào kệ
      if (shelves[i].last_alert_sent) {
        shelves[i].last_alert_sent = false;
        String restockMsg = "✅ *[BỔ SUNG HÀNG THÀNH CÔNG]*\n"
                            "Kệ *[" + String(shelves[i].id) + "] - " + shelves[i].name + "* đã được nạp thêm hàng!\n"
                            "- Số lượng hiện tại: *" + String(shelves[i].current_qty) + "* món\n"
                            "- Tải trọng: *" + String((int)shelves[i].current_weight) + "g*";
        bot.sendMessage(CHAT_ID, restockMsg, "Markdown");
        Serial.printf("[TELEGRAM] Đã gửi thông báo bổ sung hàng kệ %s\n", shelves[i].id);
        delay(300);
      }
    }
  }

  // Nếu có kệ thiếu hàng cần cảnh báo
  if (alertCount > 0) {
    String header = "🚨 *[CẢNH BÁO KHO HÀNG - NHÓM 22]*\n\n";
    String footer = "\n👉 *Vui lòng tiếp thêm hàng vào kệ!*";
    String fullMsg = header + alertMsg + footer;

    Serial.println("[TELEGRAM] Đang gửi thông báo cảnh báo qua Bot...");
    bool sent = bot.sendMessage(CHAT_ID, fullMsg, "Markdown");
    if (sent) {
      Serial.println("[TELEGRAM] Gửi thông báo thành công!");
    } else {
      Serial.println("[TELEGRAM FAILED] Không thể gửi thông báo!");
    }
  }
}

// ==========================================
// 9. XỬ LÝ LỆNH ĐIỀU KHIỂN TỪ TELEGRAM (TƯƠNG TÁC 2 CHIỀU)
// ==========================================
void handleTelegramIncoming(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String senderChatId = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    String fromName = bot.messages[i].from_name;

    Serial.printf("[TELEGRAM IN] Từ: %s (%s): %s\n", fromName.c_str(), senderChatId.c_str(), text.c_str());

    if (text == "/start" || text == "/help") {
      String welcome = "Xin chào *" + fromName + "*!\n";
      welcome += "🤖 *HỆ THỐNG SMART SHELF - NHÓM 22*\n\n";
      welcome += "Các lệnh bạn có thể gửi:\n";
      welcome += "👉 /status : Xem tình trạng 3 kệ hàng\n";
      welcome += "👉 /tare_k1 : Hiệu chuẩn 0g cho Kệ 1 (Coca)\n";
      welcome += "👉 /tare_k2 : Hiệu chuẩn 0g cho Kệ 2 (BanhOishi)\n";
      welcome += "👉 /tare_k3 : Hiệu chuẩn 0g cho Kệ 3 (MiHaoHao)\n";
      bot.sendMessage(senderChatId, welcome, "Markdown");
    }
    else if (text == "/status") {
      String statusMsg = "📊 *TÌNH TRẠNG 3 KỆ HÀNG:*\n\n";
      for (int s = 0; s < 3; s++) {
        statusMsg += (shelves[s].is_low_stock ? "🔴" : "🟢");
        statusMsg += " *[" + String(shelves[s].id) + "] " + shelves[s].name + "*: ";
        statusMsg += String(shelves[s].current_qty) + " món (" + String((int)shelves[s].current_weight) + "g)";
        if (shelves[s].is_low_stock) {
          statusMsg += " ⚠️ *THIẾU HÀNG! (Ngưỡng: " + String(shelves[s].min_qty) + ")*\n";
        } else {
          statusMsg += " (Đủ hàng)\n";
        }
      }
      bot.sendMessage(senderChatId, statusMsg, "Markdown");
    }
    else if (text.equalsIgnoreCase("/tare_k1") || text.equalsIgnoreCase("/tare k1")) {
      shelves[0].scale.tare();
      shelves[0].current_weight = 0;
      shelves[0].current_qty = 0;
      shelves[0].is_low_stock = true;
      bot.sendMessage(senderChatId, "✅ Đã Tare (về 0g) cho Kệ 1 (Coca)!", "Markdown");
      publishTelemetry();
    }
    else if (text.equalsIgnoreCase("/tare_k2") || text.equalsIgnoreCase("/tare k2")) {
      shelves[1].scale.tare();
      shelves[1].current_weight = 0;
      shelves[1].current_qty = 0;
      shelves[1].is_low_stock = true;
      bot.sendMessage(senderChatId, "✅ Đã Tare (về 0g) cho Kệ 2 (BanhOishi)!", "Markdown");
      publishTelemetry();
    }
    else if (text.equalsIgnoreCase("/tare_k3") || text.equalsIgnoreCase("/tare k3")) {
      shelves[2].scale.tare();
      shelves[2].current_weight = 0;
      shelves[2].current_qty = 0;
      shelves[2].is_low_stock = true;
      bot.sendMessage(senderChatId, "✅ Đã Tare (về 0g) cho Kệ 3 (MiHaoHao)!", "Markdown");
      publishTelemetry();
    }
  }
}

// ==========================================
// 10. NHẬN LỆNH ĐIỀU KHIỂN TỪ MQTT (DOWNLINK)
// ==========================================
void handleIncomingCommand(const char* payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.println("[CMD ERROR] Chuỗi JSON không hợp lệ!");
    return;
  }

  const char* cmd = doc["cmd"] | "";

  // Lệnh 1: Cài đặt cấu hình kệ ({"cmd":"set","id":"K1","name":"Pepsi","unit":330,"min":2})
  if (strcmp(cmd, "set") == 0) {
    const char* targetId = doc["id"] | "";
    for (int i = 0; i < 3; i++) {
      if (strcmp(targetId, shelves[i].id) == 0) {
        if (!doc["name"].isNull()) shelves[i].name = doc["name"].as<String>();
        if (!doc["unit"].isNull()) shelves[i].unit_weight = doc["unit"].as<float>();
        if (!doc["min"].isNull())  shelves[i].min_qty = doc["min"].as<int>();

        saveShelfConfig(i);

        // Tính lại số lượng tức thời
        shelves[i].current_qty = (shelves[i].unit_weight > 0) ? round(shelves[i].current_weight / shelves[i].unit_weight) : 0;
        shelves[i].is_low_stock = (shelves[i].current_qty <= shelves[i].min_qty);

        Serial.printf("[CMD SET] Đã cập nhật kệ %s: Tên=%s, Đơn vị=%.0fg, Ngưỡng=%d\n",
                      shelves[i].id, shelves[i].name.c_str(), shelves[i].unit_weight, shelves[i].min_qty);
        publishTelemetry();
        return;
      }
    }
  }
  // Lệnh 2: Tare (về 0g) ({"cmd":"tare","id":"K1"})
  else if (strcmp(cmd, "tare") == 0) {
    const char* targetId = doc["id"] | "";
    for (int i = 0; i < 3; i++) {
      if (strcmp(targetId, shelves[i].id) == 0) {
        shelves[i].scale.tare();
        shelves[i].current_weight = 0;
        shelves[i].current_qty = 0;
        shelves[i].is_low_stock = true;
        Serial.printf("[CMD TARE] Đã hiệu chuẩn 0g cho kệ %s\n", shelves[i].id);
        publishTelemetry();
        return;
      }
    }
  }
  // Lệnh 3: Yêu cầu cập nhật trạng thái ngay lập tức
  else if (strcmp(cmd, "status") == 0) {
    publishTelemetry();
  }
}

// Callback khi có tin nhắn từ MQTT Broker
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char msgBuffer[256];
  if (length >= sizeof(msgBuffer)) length = sizeof(msgBuffer) - 1;
  memcpy(msgBuffer, payload, length);
  msgBuffer[length] = '\0';

  Serial.printf("[MQTT SUB] Nhận tin từ topic [%s]: %s\n", topic, msgBuffer);
  handleIncomingCommand(msgBuffer);
}

// ==========================================
// 11. TỰ ĐỘNG KẾT NỐI & TÁI KẾT NỐI MQTT
// ==========================================
bool reconnectMQTT() {
  if (mqttClient.connected()) return true;

  Serial.print("[MQTT] Đang kết nối tới Broker...");
  String clientId = "ESP32_SmartShelf_" + String((uint32_t)ESP.getEfuseMac(), HEX);

  if (mqttClient.connect(clientId.c_str())) {
    Serial.println(" Thành công!");
    mqttClient.subscribe(TOPIC_CMD);
    Serial.printf("[MQTT] Đã đăng ký lắng nghe topic: %s\n", TOPIC_CMD);
    publishTelemetry();
    return true;
  } else {
    Serial.printf(" Thất bại, mã lỗi rc=%d\n", mqttClient.state());
    return false;
  }
}

// ==========================================
// 12. XỬ LÝ LỆNH TỪ SERIAL MONITOR (TEST NHANH)
// ==========================================
void handleSerialCommands() {
  if (!Serial.available()) return;
  String text = Serial.readStringUntil('\n');
  text.trim();
  if (text.length() == 0) return;

  if (text.startsWith("/help")) {
    Serial.println("--- LENH SERIAL ---");
    Serial.println("👉 /tare K1 (hoac K2, K3) : Ve 0g");
    Serial.println("👉 /set K1 Coca 330 3    : Cai dat ke");
    Serial.println("👉 /status               : Gui telemetry MQTT ngay");
    return;
  }

  if (text.startsWith("/tare")) {
    String id = text.substring(6); id.trim(); id.toUpperCase();
    for (int i = 0; i < 3; i++) {
      if (id == shelves[i].id) {
        shelves[i].scale.tare();
        shelves[i].current_weight = 0;
        shelves[i].current_qty = 0;
        shelves[i].is_low_stock = true;
        Serial.println("✅ Đã Tare kệ " + id);
        publishTelemetry();
        return;
      }
    }
  }

  if (text.startsWith("/status")) {
    publishTelemetry();
    return;
  }
}

// ==========================================
// 13. SETUP & KHỞI TẠO HỆ THỐNG
// ==========================================
void setup() {
  Serial.begin(115200);

  // Khởi tạo màn hình OLED I2C (SDA=21, SCL=22)
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed!");
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 15);
  display.println("SMART SHELF - NHOM 22");
  display.println("Connecting WiFi...");
  display.display();

  // Đọc cấu hình đã lưu trong Flash
  loadPreferences();

  // Khởi tạo chân LED và cảm biến HX711 cho 3 kệ
  for (int i = 0; i < 3; i++) {
    pinMode(shelves[i].led_pin, OUTPUT);
    digitalWrite(shelves[i].led_pin, LOW);

    shelves[i].scale.begin(shelves[i].dout_pin, shelves[i].sck_pin);
    shelves[i].scale.set_scale(420.0); // Hiệu chuẩn 420 xung / 1kg
  }

  // Kết nối Wi-Fi (Wokwi-GUEST)
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());

  // Cấu hình Telegram Client trực tiếp từ ESP32
  secured_client.setInsecure(); // Bỏ qua SSL cert để kết nối Telegram nhanh và ổn định
  secured_client.setTimeout(3000);

  // Gửi thông báo khởi động hệ thống qua Telegram
  Serial.println("[TELEGRAM] Đang gửi thông báo khởi động...");
  String bootMsg = "🚀 *[SMART SHELF - NHÓM 22]*\n"
                   "Hệ thống đã khởi động và kết nối mạng thành công!\n"
                   "- IP thiết bị: `" + WiFi.localIP().toString() + "`\n"
                   "- Gửi /status để kiểm tra kệ hàng bất kỳ lúc nào.";
  bot.sendMessage(CHAT_ID, bootMsg, "Markdown");

  // Cấu hình MQTT Client
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);

  // Kết nối lần đầu tới MQTT
  reconnectMQTT();

  display.clearDisplay();
  display.setCursor(0, 20);
  display.println("WiFi: Connected!");
  display.println("MQTT: broker.hivemq");
  display.display();
  delay(1200);

  Serial.println("\n=== HỆ THỐNG SMART SHELF MQTT & TELEGRAM ĐÃ SẴN SÀNG ===");
}

// ==========================================
// 14. VÒNG LẶP CHÍNH (LOOP)
// ==========================================
void loop() {
  unsigned long now = millis();

  // 1. Duy trì kết nối MQTT (Không chặn chương trình)
  if (!mqttClient.connected()) {
    if (now - lastMqttReconnectAttempt > mqttReconnectInterval) {
      lastMqttReconnectAttempt = now;
      reconnectMQTT();
    }
  } else {
    mqttClient.loop();
  }

  // 2. Kiểm tra lệnh từ cổng Serial (để test trên mô phỏng Wokwi)
  handleSerialCommands();

  // 3. Kiểm tra và nhận lệnh từ người dùng trên Telegram (mỗi 3.5 giây)
  if (now - lastTelegramCheck > telegramCheckInterval) {
    lastTelegramCheck = now;
    if (WiFi.status() == WL_CONNECTED) {
      int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      while (numNewMessages) {
        handleTelegramIncoming(numNewMessages);
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      }
    }
  }

  // 4. Đọc cảm biến định kỳ, cập nhật màn hình, gửi Telemetry MQTT và thông báo Telegram
  if (now - lastTelemetryTime > telemetryInterval) {
    lastTelemetryTime = now;

    // Đọc cân nặng và tính toán số lượng từng kệ
    for (int i = 0; i < 3; i++) {
      float w = shelves[i].scale.get_units(3) * 1000.0; // Đổi kg -> gram
      if (w < 0) w = 0;
      shelves[i].current_weight = w;

      if (shelves[i].unit_weight > 0) {
        shelves[i].current_qty = round(shelves[i].current_weight / shelves[i].unit_weight);
      } else {
        shelves[i].current_qty = 0;
      }

      // Kích hoạt đèn LED cảnh báo tại chỗ
      if (shelves[i].current_qty <= shelves[i].min_qty) {
        shelves[i].is_low_stock = true;
        digitalWrite(shelves[i].led_pin, HIGH); // Bật LED đỏ
      } else {
        shelves[i].is_low_stock = false;
        digitalWrite(shelves[i].led_pin, LOW);  // Tắt LED đỏ
      }
    }

    // Hiển thị ra màn hình OLED
    updateDisplay();

    // Publish gói tin trạng thái lên MQTT Broker (cho Node-RED nếu có)
    publishTelemetry();

    // Tự động kiểm tra và gửi cảnh báo trực tiếp tới Telegram bot
    checkAndSendTelegramAlerts();
  }
}
