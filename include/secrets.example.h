// Copy this file to include/secrets.h and fill in local values.
// secrets.h is ignored by Git. Leave values empty to disable Telegram.
#define TELEGRAM_BOT_TOKEN ""
#define TELEGRAM_CHAT_IDS { "", "" }

// For physical hardware with ten SSD1306 modules, connect each module to a
// separate TCA9548A channel and set this to 1 in secrets.h.
// Wokwi uses its configured per-display addresses and keeps this at 0.
#define OLED_USE_TCA9548A 0
