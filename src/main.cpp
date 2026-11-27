#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// ==========================================
// 1. CẤU HÌNH 10 MÀN HÌNH OLED RIÊNG BIỆT (ESL)
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// 10 Con trỏ Màn hình OLED riêng cho 10 Kệ hàng (Electronic Shelf Labels - ESL)
Adafruit_SSD1306* oleds[10];

// Wokwi mô phỏng 10 địa chỉ riêng. Phần cứng dùng 10 OLED cùng địa chỉ cần TCA9548A.
#ifndef OLED_USE_TCA9548A
#define OLED_USE_TCA9548A 0
#endif

#if OLED_USE_TCA9548A
// Mỗi TCA9548A chỉ có 8 kênh, nên 10 OLED cần 2 bộ: 0x70 (K01-K08) và 0x71 (K09-K10, chân A0 = VCC).
const uint8_t OLED_I2C_ADDR = 0x3C;
const uint8_t OLED_MUX_ADDRS[2] = {0x70, 0x71};
const uint8_t OLED_CHANNELS_PER_MUX = 8;
#else
const uint8_t OLED_ADDRS[10] = {
  0x30, 0x31, 0x32, 0x33, 0x34,
  0x35, 0x36, 0x37, 0x38, 0x39
};
#endif

// ==========================================
// 2. CẤU HÌNH MẠNG WIFI & MQTT BROKER
// ==========================================
const char* ssid = "Wokwi-GUEST";
const char* password = "";

const char* mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 1883;

const char* TOPIC_TELEMETRY = "nhom22/smartshelf/telemetry"; // ESP32 -> Broker -> Web/Node-RED
const char* TOPIC_CMD       = "nhom22/smartshelf/cmd";       // Web/Node-RED -> ESP32

// Bản tin 10 kệ dài khoảng 1.040-1.110 byte (hơn nữa nếu đổi tên sản phẩm). PubSubClient chỉ gửi được
// khi 5 + 2 + độ dài topic + độ dài nội dung <= bộ đệm, nên bộ đệm 1024 byte (tối đa 990 byte nội dung) không đủ.
const uint16_t MQTT_BUFFER_SIZE = 2048;
const size_t   JSON_BUFFER_SIZE = 1536;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences prefs;

// ==========================================
// 3. CẤU HÌNH TELEGRAM BOT TRỰC TIẾP
// ==========================================
const char* BOT_TOKEN = TELEGRAM_BOT_TOKEN;
const char* CHAT_IDS[] = TELEGRAM_CHAT_IDS;
const int NUM_USERS = sizeof(CHAT_IDS) / sizeof(CHAT_IDS[0]);
const bool telegramConfigured = (BOT_TOKEN[0] != '\0' && CHAT_IDS[0][0] != '\0');

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

void sendTelegramToAll(const String& message, const String& parseMode = "Markdown") {
  if (!telegramConfigured) return;
  for (int u = 0; u < NUM_USERS; u++) {
    if (CHAT_IDS[u][0] == '\0') continue;
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
};

// Chân Clock SCK chung cho toàn bộ 10 module HX711 trên sơ đồ Wokwi
const int HX711_COMMON_SCK = 18;

// Danh sách 10 Kệ hàng khớp 100% với sơ đồ diagram.json
Shelf shelves[10] = {
  {"K01", "Coca-Cola 330ml", 330.0, 3, 19, HX711_COMMON_SCK, 2,  0, 0.0, 0, false},
  {"K02", "Mi Hao Hao",      75.0, 5,  5, HX711_COMMON_SCK, 4,  0, 0.0, 0, false},
  {"K03", "Banh Oishi",      50.0, 4, 23, HX711_COMMON_SCK, 27, 0, 0.0, 0, false},
  {"K04", "Sua Vinamilk",   190.0, 4, 17, HX711_COMMON_SCK, 14, 0, 0.0, 0, false},
  {"K05", "Bot giat OMO",   800.0, 2, 16, HX711_COMMON_SCK, 12, 0, 0.0, 0, false},
  {"K06", "Dau an Simply",  920.0, 3, 32, HX711_COMMON_SCK, 13, 0, 0.0, 0, false},
  {"K07", "Mam Nam Ngu",    550.0, 3, 36, HX711_COMMON_SCK, 15, 0, 0.0, 0, false},
  {"K08", "Banh Chocopie",  198.0, 2, 34, HX711_COMMON_SCK, 25, 0, 0.0, 0, false},
  {"K09", "Trung Ba Huan",  600.0, 2, 35, HX711_COMMON_SCK, 26, 0, 0.0, 0, false},
  {"K10", "Tuong ot Chinsu", 280.0, 3, 39, HX711_COMMON_SCK, 33, 0, 0.0, 0, false}
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
bool readAllScales() {
  // Clock được dùng chung, vì vậy chỉ đọc khi tất cả HX711 đã báo dữ liệu sẵn sàng.
  unsigned long start = millis();
  bool allReady = false;
  while (millis() - start < 35) {
    allReady = true;
    for (int i = 0; i < 10; i++) {
      if (digitalRead(shelves[i].dout_pin) == HIGH) {
        allReady = false;
        break;
      }
    }
    if (allReady) break;
    delayMicroseconds(100);
  }
  if (!allReady) {
    Serial.println("[HX711] Bo qua mau: co cam bien chua san sang");
    return false;
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
  return true;
}

bool selectOLEDChannel(uint8_t channel) {
#if OLED_USE_TCA9548A
  if (channel >= 2 * OLED_CHANNELS_PER_MUX) return false;
  const uint8_t muxIndex = channel / OLED_CHANNELS_PER_MUX;
  const uint8_t muxChannel = channel % OLED_CHANNELS_PER_MUX;
  bool ok = true;
  // Tắt bộ còn lại trước, để không có hai OLED cùng địa chỉ 0x3C trên bus cùng lúc.
  for (uint8_t m = 0; m < 2; m++) {
    Wire.beginTransmission(OLED_MUX_ADDRS[m]);
    Wire.write(m == muxIndex ? (uint8_t)(1u << muxChannel) : (uint8_t)0);
    if (Wire.endTransmission() != 0 && m == muxIndex) ok = false;
  }
  return ok;
#else
  (void)channel;
  return true;
#endif
}

// ==========================================
// 7. CẬP NHẬT 10 MÀN HÌNH OLED RIÊNG BIỆT (ESL)
// ==========================================
void updateDisplays() {
  for (int i = 0; i < 10; i++) {
    if (!oleds[i]) continue;
    if (!selectOLEDChannel(i)) continue;
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
  doc["source"] = "hardware";
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

  // Kiểm tra độ dài trước khi ghi: bản tin bị cắt ngắn sẽ là JSON hỏng.
  size_t need = measureJson(doc);
  if (need >= JSON_BUFFER_SIZE) {
    Serial.printf("[MQTT PUB] LOI: ban tin %u byte vuot bo dem JSON %u byte\n",
                  (unsigned)need, (unsigned)JSON_BUFFER_SIZE);
    return;
  }
  static char buffer[JSON_BUFFER_SIZE];
  size_t n = serializeJson(doc, buffer, sizeof(buffer));

  bool success = mqttClient.publish(TOPIC_TELEMETRY, buffer);
  if (success) {
    Serial.printf("[MQTT PUB] -> %s (10 kệ: %d bytes)\n", TOPIC_TELEMETRY, (int)n);
  } else {
    Serial.printf("[MQTT PUB] LOI: gui that bai (%d bytes, bo dem %u)\n",
                  (int)n, (unsigned)mqttClient.getBufferSize());
  }
}

// ==========================================
// 9. TƯƠNG TÁC LỆNH TELEGRAM BOT
// ==========================================
bool isShelfMatch(const char* target, const char* shelfId);

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
    else if (text.startsWith("/tare ")) {
      String targetId = text.substring(6);
      targetId.trim();
      int shelfIndex = -1;
      for (int s = 0; s < 10; s++) {
        if (isShelfMatch(targetId.c_str(), shelves[s].id)) {
          shelfIndex = s;
          break;
        }
      }

      if (shelfIndex < 0) {
        bot.sendMessage(senderChatId, "Mã kệ không hợp lệ. Dùng /tare K01 đến /tare K10.");
        continue;
      }

      Shelf& shelf = shelves[shelfIndex];
      shelf.tare_offset += (long)((shelf.current_weight / 1000.0) * 420.0);
      shelf.current_weight = 0;
      shelf.current_qty = 0;
      shelf.is_low_stock = (shelf.current_qty <= shelf.min_qty);
      digitalWrite(shelf.led_pin, shelf.is_low_stock ? HIGH : LOW);
      publishTelemetry();

      String reply = "✅ Đã hiệu chuẩn kệ *" + String(shelf.id) + "* về 0g.";
      bot.sendMessage(senderChatId, reply, "Markdown");
      Serial.printf("[TELEGRAM TARE] Đã Tare kệ %s từ Telegram\n", shelf.id);
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
// 10. XỬ LÝ LỆNH DOWNLINK (MQTT CMD)
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
  (void)topic;
  char msgBuffer[256];
  if (length >= sizeof(msgBuffer)) {
    Serial.printf("[MQTT CMD] Bo qua payload qua dai: %u bytes\n", length);
    return;
  }
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
// 11. SETUP KHỞI TẠO HỆ THỐNG
// ==========================================
void setup() {
  Serial.begin(115200);

  // 1. Khởi tạo I2C (SDA=21, SCL=22); chế độ TCA9548A chọn từng kênh OLED.
  Wire.begin(21, 22);
  for (int i = 0; i < 10; i++) {
    oleds[i] = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#if OLED_USE_TCA9548A
    selectOLEDChannel(i);
    oleds[i]->begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
#else
    oleds[i]->begin(SSD1306_SWITCHCAPVCC, OLED_ADDRS[i]);
#endif
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

  if (telegramConfigured) {
    String bootMsg = "🏪 *[TIỆM TẠP HÓA 4.0 - 10 KỆ HÀNG]*\n"
                     "Hệ thống đã khởi động thành công với 10 cảm biến và 10 màn hình ESL!\n"
                     "- IP thiết bị: `" + WiFi.localIP().toString() + "`\n"
                     "- Gửi /status để kiểm tra tồn kho.";
    sendTelegramToAll(bootMsg);
  } else {
    Serial.println("[TELEGRAM] Chua cau hinh token/chat ID; bo qua Telegram");
  }

  // 7. Cấu hình MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  if (!mqttClient.setBufferSize(MQTT_BUFFER_SIZE)) {
    Serial.printf("[MQTT] LOI: khong cap phat duoc bo dem %u byte\n", (unsigned)MQTT_BUFFER_SIZE);
  }
  reconnectMQTT();

  Serial.println("=== HỆ THỐNG 10 KỆ HÀNG TẠP HÓA (10 OLED ESL) ĐÃ SẴN SÀNG ===");
}

// ==========================================
// 12. VÒNG LẶP CHÍNH (LOOP)
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
    if (WiFi.status() == WL_CONNECTED && telegramConfigured) {
      if (telegramConfigured) {
        int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        while (numNewMessages) {
          handleTelegramIncoming(numNewMessages);
          numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        }
      }
    }
  }

  // 3. Chu kỳ đọc 10 cảm biến HX711, cập nhật 10 OLED và publish MQTT
  if (now - lastTelemetryTime > telemetryInterval) {
    lastTelemetryTime = now;

    // Chỉ cập nhật UI/telemetry khi đủ 10 HX711 sẵn sàng để đọc đồng bộ.
    if (!readAllScales()) return;

    // Cập nhật 10 màn hình OLED ESL riêng biệt
    updateDisplays();

    // Gửi Telemetry 10 kệ qua MQTT
    publishTelemetry();

    // Cảnh báo tồn kho chỉ gửi từ Node-RED để tránh gửi trùng từ ESP32.
  }
}
