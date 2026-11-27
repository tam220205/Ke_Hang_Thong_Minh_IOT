// Copy this file to include/secrets.h and fill in local values.
// secrets.h is ignored by Git. Never put real secrets in this example file.

// ---------- Telegram ----------
// Leave empty to disable Telegram. Only chats listed here may use bot commands.
#define TELEGRAM_BOT_TOKEN ""
#define TELEGRAM_CHAT_IDS { "", "" }

// ---------- MQTT broker (OWASP I2) ----------
// Default: HiveMQ public broker over TLS (port 8883, server certificate verified).
// For a private broker (Mosquitto in docker-compose, HiveMQ Cloud...) set host,
// port, user and password. Set MQTT_USE_TLS to 0 only for a broker on a trusted LAN.
#define MQTT_HOST "broker.hivemq.com"
#define MQTT_PORT 8883
#define MQTT_USE_TLS 1
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
// Optional: PEM root CA of your own broker. Default bundle is in mqtt_ca.h.
// #define MQTT_CA_CERT "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"

// ---------- Message signing (OWASP I2/I3) ----------
// Shared secret, at least 16 characters, also set as SHELF_HMAC_KEY in .env
// (Node-RED) and typed into the web dashboard. Commands without a valid
// HMAC-SHA256 signature are rejected; telemetry is signed with it.
// Empty = the ESP32 refuses every MQTT command (fail closed).
#define SHELF_HMAC_KEY ""

// ---------- Hardware ----------
// For physical hardware with ten SSD1306 modules (all at 0x3C), use two
// TCA9548A multiplexers: 0x70 for K01-K08 (channels 0-7) and 0x71 (A0 = VCC)
// for K09-K10 (channels 0-1). Then set this to 1 in secrets.h.
// Wokwi uses its configured per-display addresses and keeps this at 0.
#define OLED_USE_TCA9548A 0
