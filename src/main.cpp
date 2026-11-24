#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ==========================================
// 1. CẤU HÌNH 10 MÀN HÌNH OLED RIÊNG BIỆT (ESL)
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// 10 Con trỏ Màn hình OLED riêng cho 10 Kệ hàng (Electronic Shelf Labels - ESL)
Adafruit_SSD1306* oleds[10];

// 10 Địa chỉ I2C riêng biệt trên Bus I2C chung (GPIO 21 SDA, GPIO 22 SCL)
const uint8_t OLED_ADDRS[10] = {
  0x30, 0x31, 0x32, 0x33, 0x34,
  0x35, 0x36, 0x37, 0x38, 0x39
};

// ==========================================
// 2. CẤU HÌNH MẠNG WIFI & MQTT BROKER
// ==========================================
const char* ssid = "Wokwi-GUEST";
const char* password = "";

const char* mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 1883;

const char* TOPIC_TELEMETRY = "nhom22/smartshelf/telemetry"; // ESP32 -> Broker -> Web/Node-RED
const char* TOPIC_CMD       = "nhom22/smartshelf/cmd";       // Web/Node-RED -> ESP32

WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences prefs;

// ==========================================
// 3. CẤU HÌNH TELEGRAM BOT TRỰC TIẾP
// ==========================================
const char* BOT_TOKEN = "8838533602:AAERwan30IWcSVd7wOiZlTo8A68JZ1M2hM0";
const char* CHAT_IDS[] = {
  "8349343826",
  "7982908394"
};
const int NUM_USERS = sizeof(CHAT_IDS) / sizeof(CHAT_IDS[0]);

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

void sendTelegramToAll(const String& message, const String& parseMode = "Markdown") {
  for (int u = 0; u < NUM_USERS; u++) {
    bot.sendMessage(CHAT_IDS[u], message, parseMode);
    delay(150);
  }
}

unsigned long lastTelegramCheck = 0;
const unsigned long telegramCheckInterval = 3500;

// ==========================================
// 4. ĐỊNH NGHĨA 10 KỆ HÀNG TRÊN SƠ ĐỒ WOKWI
// ==========================================
struct Shelf {
  const char* id;          // "K01" .. "K10"
  String name;             // Tên sản phẩm tạp hóa
  float unit_weight;       // Khối lượng 1 đơn vị (gram)
  int min_qty;             // Ngưỡng báo động số lượng
  int dout_pin;            // Chân DT riêng biệt
  int sck_pin;             // Chân SCK chung (GPIO 18)
  int led_pin;             // Chân điều khiển LED cảnh báo
  long tare_offset;        // Offset hiệu chuẩn 0g
  float current_weight;    // gram
  int current_qty;         // số lượng hiện tại
  bool is_low_stock;       // cờ thiếu hàng
  bool last_alert_sent;    // cờ đã báo Telegram
  unsigned long last_alert_time;
};

// Chân Clock SCK chung cho toàn bộ 10 module HX711 trên sơ đồ Wokwi
const int HX711_COMMON_SCK = 18;

// Danh sách 10 Kệ hàng khớp 100% với sơ đồ diagram.json
Shelf shelves[10] = {
  {"K01", "Coca-Cola 330ml", 330.0, 3, 19, HX711_COMMON_SCK, 2,  0, 0.0, 0, false, false, 0},
  {"K02", "Mi Hao Hao",      75.0, 5,  5, HX711_COMMON_SCK, 4,  0, 0.0, 0, false, false, 0},
  {"K03", "Banh Oishi",      50.0, 4, 23, HX711_COMMON_SCK, 27, 0, 0.0, 0, false, false, 0},
  {"K04", "Sua Vinamilk",   190.0, 4, 17, HX711_COMMON_SCK, 14, 0, 0.0, 0, false, false, 0},
  {"K05", "Bot giat OMO",   800.0, 2, 16, HX711_COMMON_SCK, 12, 0, 0.0, 0, false, false, 0},
  {"K06", "Dau an Simply",  920.0, 3, 32, HX711_COMMON_SCK, 13, 0, 0.0, 0, false, false, 0},
  {"K07", "Mam Nam Ngu",    550.0, 3, 36, HX711_COMMON_SCK, 15, 0, 0.0, 0, false, false, 0},
  {"K08", "Banh Chocopie",  198.0, 2, 34, HX711_COMMON_SCK, 25, 0, 0.0, 0, false, false, 0},
  {"K09", "Trung Ba Huan",  600.0, 2, 35, HX711_COMMON_SCK, 26, 0, 0.0, 0, false, false, 0},
  {"K10", "Tuong ot Chinsu", 280.0, 3, 39, HX711_COMMON_SCK, 33, 0, 0.0, 0, false, false, 0}
};

unsigned long lastTelemetryTime = 0;
const unsigned long telemetryInterval = 2500; // Gửi MQTT mỗi 2.5s

unsigned long lastMqttReconnectAttempt = 0;
const unsigned long mqttReconnectInterval = 5000;

// ==========================================
// 5. LƯU & ĐỌC CẤU HÌNH FLASH (PREFERENCES)
// ==========================================
void loadPreferences() {
  prefs.begin("taphoa10", true);
  for (int i = 0; i < 10; i++) {
    String keyName = String(shelves[i].id) + "_n";
    String keyUnit = String(shelves[i].id) + "_u";
    String keyMin  = String(shelves[i].id) + "_m";

    if (prefs.isKey(keyName.c_str())) shelves[i].name = prefs.getString(keyName.c_str(), shelves[i].name);
    if (prefs.isKey(keyUnit.c_str())) shelves[i].unit_weight = prefs.getFloat(keyUnit.c_str(), shelves[i].unit_weight);
    if (prefs.isKey(keyMin.c_str()))  shelves[i].min_qty = prefs.getInt(keyMin.c_str(), shelves[i].min_qty);
  }
  prefs.end();
}

void saveShelfConfig(int index) {
  prefs.begin("taphoa10", false);
  String keyName = String(shelves[index].id) + "_n";
  String keyUnit = String(shelves[index].id) + "_u";
  String keyMin  = String(shelves[index].id) + "_m";

  prefs.putString(keyName.c_str(), shelves[index].name);
  prefs.putFloat(keyUnit.c_str(), shelves[index].unit_weight);
  prefs.putInt(keyMin.c_str(), shelves[index].min_qty);
  prefs.end();
}

// ==========================================
// 6. ĐỌC ĐỒNG THỜI 10 CẢM BIẾN HX711 (SIÊU TỐC ~50us)
// ==========================================
void readAllScales() {
  // Chờ cảm biến ổn định dữ liệu
  unsigned long start = millis();
  while (digitalRead(shelves[0].dout_pin) == HIGH && (millis() - start < 35)) {
    delayMicroseconds(100);
  }

  long raw[10] = {0};

  // Phát 24 xung nhịp chung trên GPIO 18 và đồng thời đọc 10 chân DT
  for (int b = 0; b < 24; b++) {
    digitalWrite(HX711_COMMON_SCK, HIGH);
    delayMicroseconds(1);
    for (int i = 0; i < 10; i++) {
      raw[i] = (raw[i] << 1) | digitalRead(shelves[i].dout_pin);
    }
    digitalWrite(HX711_COMMON_SCK, LOW);
    delayMicroseconds(1);
  }

  // Xung thứ 25 đặt Channel A, Gain 128
  digitalWrite(HX711_COMMON_SCK, HIGH);
  delayMicroseconds(1);
  digitalWrite(HX711_COMMON_SCK, LOW);
  delayMicroseconds(1);

  // Xử lý bù hai 24-bit có dấu và chuyển đổi thành khối lượng gram
  for (int i = 0; i < 10; i++) {
    if (raw[i] & 0x800000) {
      raw[i] |= 0xFF000000;
    }

    long netVal = raw[i] - shelves[i].tare_offset;
    // Chuẩn hóa theo tỉ lệ Wokwi HX711 (420 xung = 1kg = 1000g)
    float weight = ((float)netVal / 420.0) * 1000.0;
    if (weight < 0) weight = 0;
    shelves[i].current_weight = weight;

    if (shelves[i].unit_weight > 0) {
      shelves[i].current_qty = round(shelves[i].current_weight / shelves[i].unit_weight);
    } else {
      shelves[i].current_qty = 0;
    }

    // Điều khiển đèn LED đỏ cảnh báo tại chỗ cho từng kệ
    shelves[i].is_low_stock = (shelves[i].current_qty <= shelves[i].min_qty);
    digitalWrite(shelves[i].led_pin, shelves[i].is_low_stock ? HIGH : LOW);
  }
}

// ==========================================
// 7. CẬP NHẬT 10 MÀN HÌNH OLED RIÊNG BIỆT (ESL)
// ==========================================
void updateDisplays() {
  for (int i = 0; i < 10; i++) {
    if (!oleds[i]) continue;
    oleds[i]->clearDisplay();

    // Dòng 1: Banner đen trên nền trắng nổi bật tên sản phẩm và mã kệ
    oleds[i]->fillRect(0, 0, 128, 12, WHITE);
    oleds[i]->setTextColor(BLACK);
    oleds[i]->setTextSize(1);
    oleds[i]->setCursor(2, 2);
    oleds[i]->printf("[%s] %s", shelves[i].id, shelves[i].name.c_str());

    // Dòng 2: Số lượng tồn kho lớn trực quan
    oleds[i]->setTextColor(WHITE);
    oleds[i]->setTextSize(2);
    oleds[i]->setCursor(2, 17);
    oleds[i]->printf("%2d", shelves[i].current_qty);
    oleds[i]->setTextSize(1);
    oleds[i]->setCursor(28, 23);
    oleds[i]->print("mon");

    // Khối lượng hiện tại & Quy cách đóng gói & Ngưỡng min
    oleds[i]->setCursor(62, 16);
    oleds[i]->printf("%4dg", (int)shelves[i].current_weight);
    oleds[i]->setCursor(62, 26);
    oleds[i]->printf("(%dg/m)", (int)shelves[i].unit_weight);
    oleds[i]->setCursor(62, 36);
    oleds[i]->printf("Min: %2d", shelves[i].min_qty);

    // Dòng 3: Khung trạng thái cảnh báo tại kệ
    oleds[i]->drawLine(0, 48, 127, 48, WHITE);
    if (shelves[i].is_low_stock) {
      oleds[i]->fillRect(0, 50, 128, 14, WHITE);
      oleds[i]->setTextColor(BLACK);
      oleds[i]->setCursor(8, 53);
      oleds[i]->print("! CAN NHAP HANG !");
    } else {
      oleds[i]->setTextColor(WHITE);
      oleds[i]->setCursor(16, 53);
      oleds[i]->print("OK - DU HANG");
    }

    oleds[i]->display();
  }
}

// ==========================================
// 8. GỬI DỮ LIỆU TELEMETRY 10 KỆ QUA MQTT (JSON)
// ==========================================
void publishTelemetry() {
  if (!mqttClient.connected()) return;

  JsonDocument doc;
  doc["device"] = "ESP32_SmartShelf_10Tiers";
  doc["node_id"] = "NODE_01";
  doc["store"] = "TapHoaSmart";
  doc["uptime"] = millis() / 1000;

  int totalAlert = 0;
  JsonArray array = doc["shelves"].to<JsonArray>();

  for (int i = 0; i < 10; i++) {
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

  char buffer[1024];
  size_t n = serializeJson(doc, buffer);

  bool success = mqttClient.publish(TOPIC_TELEMETRY, buffer);
  if (success) {
    Serial.printf("[MQTT PUB] -> %s (10 kệ: %d bytes)\n", TOPIC_TELEMETRY, (int)n);
  }
}

// ==========================================
// 9. CẢNH BÁO TELEGRAM CHO TOÀN BỘ 10 KỆ
// ==========================================
void checkAndSendTelegramAlerts() {
  if (WiFi.status() != WL_CONNECTED) return;
  unsigned long now = millis();

  String alertMsg = "";
  int alertCount = 0;

  for (int i = 0; i < 10; i++) {
    if (shelves[i].is_low_stock) {
      if (!shelves[i].last_alert_sent || (now - shelves[i].last_alert_time >= 300000)) {
        shelves[i].last_alert_sent = true;
        shelves[i].last_alert_time = now;
        alertMsg += "⚠️ *[" + String(shelves[i].id) + "] " + shelves[i].name + "*: Còn *" + 
                    String(shelves[i].current_qty) + "* món (Tải trọng: " + 
                    String((int)shelves[i].current_weight) + "g, Min: " + 
                    String(shelves[i].min_qty) + ")\n";
        alertCount++;
      }
    } else {
      if (shelves[i].last_alert_sent) {
        shelves[i].last_alert_sent = false;
        String restockMsg = "✅ *[BỔ SUNG HÀNG THÀNH CÔNG]*\n"
                            "Kệ *[" + String(shelves[i].id) + "] - " + shelves[i].name + "* đã được nạp thêm hàng!\n"
                            "- Số lượng: *" + String(shelves[i].current_qty) + "* món (" + 
                            String((int)shelves[i].current_weight) + "g)";
        sendTelegramToAll(restockMsg);
      }
    }
  }

  if (alertCount > 0) {
    String header = "🚨 *[CẢNH BÁO TIỆM TẠP HÓA 4.0]*\n\n";
    String footer = "\n👉 *Vui lòng kiểm tra và tiếp thêm hàng vào kệ!*";
    sendTelegramToAll(header + alertMsg + footer);
    Serial.printf("[TELEGRAM] Đã gửi cảnh báo %d kệ thiếu hàng\n", alertCount);
  }
}

// ==========================================
// 10. TƯƠNG TÁC LỆNH TELEGRAM BOT
// ==========================================
void handleTelegramIncoming(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String senderChatId = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    String fromName = bot.messages[i].from_name;

    if (text == "/start" || text == "/help") {
      String welcome = "Xin chào *" + fromName + "*!\n";
      welcome += "🏪 *TIỆM TẠP HÓA THÔNG MINH 4.0 (10 KỆ HÀNG)*\n\n";
      welcome += "👉 /status : Xem tình trạng tồn kho cả 10 kệ\n";
      welcome += "👉 /tare K01 (đến K10) : Hiệu chuẩn 0g cho kệ\n";
      bot.sendMessage(senderChatId, welcome, "Markdown");
    }
    else if (text == "/status") {
      String statusMsg = "📊 *TÌNH TRẠNG 10 KỆ HÀNG TẠP HÓA:*\n\n";
      for (int s = 0; s < 10; s++) {
        statusMsg += (shelves[s].is_low_stock ? "🔴" : "🟢");
        statusMsg += " *[" + String(shelves[s].id) + "] " + shelves[s].name + "*: ";
        statusMsg += String(shelves[s].current_qty) + " món (" + String((int)shelves[s].current_weight) + "g)";
        if (shelves[s].is_low_stock) {
          statusMsg += " ⚠️ *THIẾU!*\n";
        } else {
          statusMsg += " (Đủ)\n";
        }
      }
      bot.sendMessage(senderChatId, statusMsg, "Markdown");
    }
  }
}

// Helper: So khớp mã K1 và K01
bool isShelfMatch(const char* target, const char* shelfId) {
  if (strcmp(target, shelfId) == 0) return true;
  if (strlen(target) == 2 && strlen(shelfId) == 3 && target[0] == 'K' && shelfId[0] == 'K') {
    return target[1] == shelfId[2];
  }
  if (strlen(target) == 3 && strlen(shelfId) == 2 && target[0] == 'K' && shelfId[0] == 'K') {
    return target[2] == shelfId[1];
  }
  return false;
}

// ==========================================
// 11. XỬ LÝ LỆNH DOWNLINK (MQTT CMD)
// ==========================================
void handleIncomingCommand(const char* payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) return;

  const char* cmd = doc["cmd"] | "";

  if (strcmp(cmd, "set") == 0) {
    const char* targetId = doc["id"] | "";
    for (int i = 0; i < 10; i++) {
      if (isShelfMatch(targetId, shelves[i].id)) {
        if (!doc["name"].isNull()) shelves[i].name = doc["name"].as<String>();
        if (!doc["unit"].isNull()) shelves[i].unit_weight = doc["unit"].as<float>();
        if (!doc["min"].isNull())  shelves[i].min_qty = doc["min"].as<int>();

        saveShelfConfig(i);

        shelves[i].current_qty = (shelves[i].unit_weight > 0) ? round(shelves[i].current_weight / shelves[i].unit_weight) : 0;
        shelves[i].is_low_stock = (shelves[i].current_qty <= shelves[i].min_qty);

        Serial.printf("[CMD SET] Kệ %s: Tên=%s, Đơn vị=%.0fg, Min=%d\n",
                      shelves[i].id, shelves[i].name.c_str(), shelves[i].unit_weight, shelves[i].min_qty);
        publishTelemetry();
        return;
      }
    }
  }
  else if (strcmp(cmd, "tare") == 0) {
    const char* targetId = doc["id"] | "";
    for (int i = 0; i < 10; i++) {
      if (isShelfMatch(targetId, shelves[i].id)) {
        shelves[i].tare_offset += (long)((shelves[i].current_weight / 1000.0) * 420.0);
        shelves[i].current_weight = 0;
        shelves[i].current_qty = 0;
        shelves[i].is_low_stock = true;
        Serial.printf("[CMD TARE] Đã Tare 0g cho kệ %s\n", shelves[i].id);
        publishTelemetry();
        return;
      }
    }
  }
  else if (strcmp(cmd, "status") == 0) {
    publishTelemetry();
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char msgBuffer[256];
  if (length >= sizeof(msgBuffer)) length = sizeof(msgBuffer) - 1;
  memcpy(msgBuffer, payload, length);
  msgBuffer[length] = '\0';
  handleIncomingCommand(msgBuffer);
}

bool reconnectMQTT() {
  if (mqttClient.connected()) return true;

  String clientId = "ESP32_SmartShelf_10Tiers_" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (mqttClient.connect(clientId.c_str())) {
    mqttClient.subscribe(TOPIC_CMD);
    publishTelemetry();
    return true;
  }
  return false;
}

// ==========================================
// 12. SETUP KHỞI TẠO HỆ THỐNG
// ==========================================
void setup() {
  Serial.begin(115200);

  // 1. Khởi tạo Bus I2C (SDA=21, SCL=22) và 10 Màn hình OLED ESL riêng cho từng kệ
  Wire.begin(21, 22);
  for (int i = 0; i < 10; i++) {
    oleds[i] = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
    oleds[i]->begin(SSD1306_SWITCHCAPVCC, OLED_ADDRS[i]);
  }
  // Hiển thị thông tin nhận diện sản phẩm trên cả 10 màn hình ngay từ lúc bật nguồn
  updateDisplays();

  // 2. Cấu hình chân Clock SCK chung cho 10 module HX711
  pinMode(HX711_COMMON_SCK, OUTPUT);
  digitalWrite(HX711_COMMON_SCK, LOW);

  // 3. Cấu hình chân LED (OUTPUT) và chân DT (INPUT) cho cả 10 kệ
  for (int i = 0; i < 10; i++) {
    pinMode(shelves[i].led_pin, OUTPUT);
    digitalWrite(shelves[i].led_pin, LOW);
    pinMode(shelves[i].dout_pin, INPUT);
  }

  // 4. Đọc cấu hình đã lưu trong Flash
  loadPreferences();

  // 5. Kết nối Wi-Fi Wokwi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());

  // 6. Cấu hình Telegram Client
  secured_client.setInsecure();
  secured_client.setTimeout(3000);

  String bootMsg = "🏪 *[TIỆM TẠP HÓA 4.0 - 10 KỆ HÀNG]*\n"
                   "Hệ thống đã khởi động thành công với 10 cảm biến và 10 màn hình ESL!\n"
                   "- IP thiết bị: `" + WiFi.localIP().toString() + "`\n"
                   "- Gửi /status để kiểm tra tồn kho.";
  sendTelegramToAll(bootMsg);

  // 7. Cấu hình MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);
  reconnectMQTT();

  Serial.println("=== HỆ THỐNG 10 KỆ HÀNG TẠP HÓA (10 OLED ESL) ĐÃ SẴN SÀNG ===");
}

// ==========================================
// 13. VÒNG LẶP CHÍNH (LOOP)
// ==========================================
void loop() {
  unsigned long now = millis();

  // 1. Duy trì kết nối MQTT
  if (!mqttClient.connected()) {
    if (now - lastMqttReconnectAttempt > mqttReconnectInterval) {
      lastMqttReconnectAttempt = now;
      reconnectMQTT();
    }
  } else {
    mqttClient.loop();
  }

  // 2. Kiểm tra lệnh từ Telegram Bot
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

  // 3. Chu kỳ đọc 10 cảm biến HX711, cập nhật 10 OLED và publish MQTT
  if (now - lastTelemetryTime > telemetryInterval) {
    lastTelemetryTime = now;

    // Đọc đồng thời 10 cảm biến HX711
    readAllScales();

    // Cập nhật 10 màn hình OLED ESL riêng biệt
    updateDisplays();

    // Gửi Telemetry 10 kệ qua MQTT
    publishTelemetry();

    // Kiểm tra và gửi thông báo Telegram
    checkAndSendTelegramAlerts();
  }
}
