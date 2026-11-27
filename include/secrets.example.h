// Copy this file to include/secrets.h and fill in local values.
// secrets.h is ignored by Git. Leave values empty to disable Telegram.
#define TELEGRAM_BOT_TOKEN ""
#define TELEGRAM_CHAT_IDS { "", "" }

// For physical hardware with ten SSD1306 modules (all at 0x3C), use two
// TCA9548A multiplexers: 0x70 for K01-K08 (channels 0-7) and 0x71 (A0 = VCC)
// for K09-K10 (channels 0-1). Then set this to 1 in secrets.h.
// Wokwi uses its configured per-display addresses and keeps this at 0.
#define OLED_USE_TCA9548A 0
