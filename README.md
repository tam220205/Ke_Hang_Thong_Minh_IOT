# 🏪 HỆ THỐNG KỆ HÀNG TẠP HÓA THÔNG MINH (SMART GROCERY STORE 4.0)
## MÔ HÌNH 10 KỆ HÀNG POC CHO TIỆM TẠP HÓA 50 SẢN PHẨM — NHÓM 22

> **Đồ án môn học:** Internet of Things (IoT)  
> **Nhóm thực hiện:** Nhóm 22  
> **Kho lưu trữ:** [https://github.com/tam220205/Ke_Hang_Thong_Minh_IOT](https://github.com/tam220205/Ke_Hang_Thong_Minh_IOT)  
> **Nền tảng hỗ trợ:** ESP32, Wokwi Simulator, HiveMQ MQTT, Node-RED, Docker Compose, Web Dashboard (Chart.js), Telegram Bot API.

---

## 🌟 1. TỔNG QUAN DỰ ÁN

Dự án phát triển giải pháp **Kệ hàng Thông minh (Smart Shelf)** tự động hóa việc kiểm kê và cảnh báo tồn kho theo thời gian thực cho tiệm tạp hóa quy mô vừa và nhỏ (~50 mặt hàng). 

Hệ thống được chia thành **10 phân khu / kệ hàng chính (PoC 10 Kệ)**, mỗi kệ được trang bị:
1. **Cảm biến trọng lượng loadcell + HX711:** Cân và tính toán số lượng tồn kho theo thời gian thực.
2. **Màn hình OLED SSD1306 (Electronic Shelf Label - ESL):** Hiển thị rõ ràng tên sản phẩm, số lượng tồn lớn, tải trọng gram và trạng thái cảnh báo tại kệ.
3. **Đèn LED cảnh báo đỏ tại chỗ:** Báo động ngay lập tức khi hàng dưới mức an toàn.
4. **Hệ thống cảnh báo Telegram Bot:** Gửi tin nhắn định dạng chuẩn `MarkdownV2` đa người nhận khi cạn hàng.
5. **Web Dashboard tương tác:** Bảng điều khiển trực quan 10 kệ, biểu đồ tải trọng thời gian thực và bộ lọc danh mục.
6. **Đóng gói Docker Compose turnkey:** Triển khai Node-RED, Mosquitto MQTT Broker và Web Nginx chỉ với 1 câu lệnh.

---

## 📦 2. DANH MỤC 10 KỆ HÀNG TẠP HÓA ĐƯỢC CHUẨN HÓA

| Màn Hình | Địa Chỉ I2C | Mã Kệ | Tên Sản Phẩm | Phân Loại | Khối Lượng Đơn Vị | Ngưỡng Báo (Min) | Vị Trí Wokwi |
| :---: | :---: | :---: | :--- | :--- | :---: | :---: | :---: |
| **OLED 1** | `0x30` | **K01** | **Coca-Cola 330ml** | Đồ uống giải khát | 330g / lon | $\le$ 3 lon | Cột Trái (Tầng 1) |
| **OLED 2** | `0x31` | **K02** | **Mì Hảo Hảo** | Thực phẩm ăn liền | 75g / gói | $\le$ 5 gói | Cột Trái (Tầng 2) |
| **OLED 3** | `0x32` | **K03** | **Bánh Oishi** | Bánh kẹo & Snack | 50g / gói | $\le$ 4 gói | Cột Trái (Tầng 3) |
| **OLED 4** | `0x33` | **K04** | **Sữa Vinamilk** | Sữa tươi | 190g / hộp | $\le$ 4 hộp | Cột Trái (Tầng 4) |
| **OLED 5** | `0x34` | **K05** | **Bột giặt OMO** | Hóa mỹ phẩm | 800g / túi | $\le$ 2 túi | Cột Trái (Tầng 5) |
| **OLED 6** | `0x35` | **K06** | **Dầu ăn Simply** | Gia vị nấu ăn | 920g / chai | $\le$ 3 chai | Cột Phải (Tầng 1) |
| **OLED 7** | `0x36` | **K07** | **Nước mắm Nam Ngư** | Gia vị & Nước chấm | 550g / chai | $\le$ 3 chai | Cột Phải (Tầng 2) |
| **OLED 8** | `0x37` | **K08** | **Bánh Chocopie** | Bánh kẹo đóng hộp | 198g / hộp | $\le$ 2 hộp | Cột Phải (Tầng 3) |
| **OLED 9** | `0x38` | **K09** | **Trứng gà Ba Huân** | Thực phẩm tươi | 600g / vỉ (10q) | $\le$ 2 vỉ | Cột Phải (Tầng 4) |
| **OLED 10** | `0x39` | **K10** | **Tương ớt Chinsu** | Gia vị & Đồ chấm | 280g / chai | $\le$ 3 chai | Cột Phải (Tầng 5) |

---

## 🔌 3. SƠ ĐỒ ĐI DÂY & PHÂN BỔ 23 CHÂN GPIO (ESP32)

Toàn bộ hệ thống sử dụng kiến trúc **Bus Daisy-Chain song song**, tuyệt đối không xung đột chân:

* **Bus I2C chung (10 Màn hình OLED SSD1306):**
  * `GPIO 21` (SDA) nối song song 10 chân SDA của 10 màn hình.
  * `GPIO 22` (SCL) nối song song 10 chân SCL của 10 màn hình.
  * Địa chỉ I2C phân tán: `0x30` $\rightarrow$ `0x39`.
* **Clock SCK chung (10 Module HX711):**
  * `GPIO 18` nối song song chân SCK của cả 10 module HX711 (cho phép đọc song song 10 cảm biến trong ~50µs).
* **10 Chân Dữ liệu DT riêng biệt:**
  * K01..K05 (Cột Trái): `GPIO 19`, `GPIO 5`, `GPIO 23`, `GPIO 17`, `GPIO 16`.
  * K06..K10 (Cột Phải): `GPIO 32`, `GPIO 36 (VP)`, `GPIO 34`, `GPIO 35`, `GPIO 39 (VN)`.
* **10 Chân Đèn LED đỏ cảnh báo:**
  * K01..K05 (Cột Trái): `GPIO 2`, `GPIO 4`, `GPIO 27`, `GPIO 14`, `GPIO 12`.
  * K06..K10 (Cột Phải): `GPIO 13`, `GPIO 15`, `GPIO 25`, `GPIO 26`, `GPIO 33`.

---

## 🏗️ 4. KIẾN TRÚC HỆ THỐNG IOT

```
┌─────────────────────────────────────────────────────────────┐
│                       ESP32 FIRMWARE                        │
│   • 10 Loadcells + Parallel HX711 Read (GPIO 18 Clock)      │
│   • 10 OLED ESL Displays (I2C 0x30..0x39)                   │
│   • Wi-Fi Wokwi + Direct Telegram Bot Alerts                │
└──────────────────────────────┬──────────────────────────────┘
                               │ MQTT Telemetry / Cmd
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                    HIVEMQ PUBLIC BROKER                     │
│   • Topic Telemetry: nhom22/smartshelf/telemetry            │
│   • Topic Command:   nhom22/smartshelf/cmd                  │
└──────────────┬──────────────────────────────┬───────────────┘
               │                              │
               ▼                              ▼
┌─────────────────────────────┐  ┌────────────────────────────┐
│      NODE-RED IOT SERVER    │  │    WEB DASHBOARD (HTML/JS) │
│ • 10-Shelf Grocery Engine   │  │ • Lưới 10 Kệ Card sinh động│
│ • Thống kê tồn kho tổng hợp │  │ • Chart.js 10 đường实时   │
│ • Cảnh báo Telegram MDv2    │  │ • Nút tương tác Mua/Nhập   │
│ • Gửi lệnh Tare / Set Config│  │ • Xuất báo cáo CSV/Excel   │
└─────────────────────────────┘  └────────────────────────────┘
```

---

## 🚀 5. HƯỚNG DẪN KHỞI CHẠY NHANH

### Cách 1: Khởi chạy bằng Docker Compose (Khuyên dùng)
```bash
# Khởi động toàn bộ cụm dịch vụ (Node-RED, Mosquitto, Nginx Dashboard)
docker compose up -d

# Truy cập các dịch vụ:
# - Web Dashboard:  http://localhost:8088
# - Node-RED Flow:  http://localhost:1880
# - Mosquitto MQTT: localhost:1883 & ws://localhost:9001
```

### Cách 2: Chạy mô phỏng ESP32 trên Wokwi
1. Mở dự án trong Visual Studio Code với extension **Wokwi Simulator**.
2. Nhấn `F1` $\rightarrow$ chọn `Wokwi: Start Simulator` (hoặc mở tệp [diagram.json](file:///c:/Users/boy/Documents/PlatformIO/Projects/tesst/diagram.json)).
3. Cả 10 màn hình OLED sẽ sáng và hiển thị tên từng sản phẩm tương ứng.

### Cách 3: Mở Web Dashboard trực tiếp
Mở tệp [web/index.html](file:///c:/Users/boy/Documents/PlatformIO/Projects/tesst/web/index.html) bằng bất kỳ trình duyệt nào.

---

## 📱 6. ĐỊNH DẠNG TIN NHẮN CẢNH BÁO TELEGRAM (MARKDOWNV2)

```
🚨 CẢNH BÁO KHO HÀNG 🚨
━━━━━━━━━━━━━━━━━━━━━━
🏪 Tiệm Tạp Hóa Smart — Nhóm 22
📋 2 kệ hàng cần tiếp hàng ngay!

🔴 HẾT HÀNG — CẦN XUẤT KHO NGAY:
  • K05  Bột giặt OMO 800g
    → Tồn: 0 món ┃ KL: 0g ┃ Min: 2

🟡 SẮP HẾT — CẦN BỔ SUNG:
  • K01  Coca-Cola 330ml
    → Tồn: 2 món ┃ KL: 660g ┃ [██░░░] 33%

━━━━━━━━━━━━━━━━━━━━━━
🕐 Thời gian: 25/11/2026, 00:15:00
⏱ Uptime ESP32: 2h 15m

📲 Hệ thống IoT Smart Shelf — ESP32 + Node-RED
👉 Vui lòng kiểm tra kệ hàng ngay!
```

---

## 📂 7. CẤU TRÚC THƯ MỤC DỰ ÁN

```
.
├── README.md                              # Tài liệu tổng quan dự án (File này)
├── HE_THONG_IOT_VA_DANH_SACH_THIET_BI.md  # Thuyết minh chi tiết kiến trúc & danh mục thiết bị
├── KE_HOACH_NANG_CAP_10_KE_TAP_HOA.md     # Kế hoạch & Nhật ký nâng cấp từ Bước 1 -> Bước 8
├── diagram.json                           # Sơ đồ mạch phần cứng Wokwi (10 OLED, 10 Loadcell, 10 LED)
├── nodered_flow.json                      # Luồng Node-RED chính (10-Shelf Engine + Telegram MDv2)
├── combined_flows.json                    # Bản sao lưu toàn bộ flow Node-RED
├── docker-compose.yml                     # Cấu hình triển khai container Docker Compose
├── docker/                                # Thư mục cấu hình Docker (Mosquitto & Node-RED data)
├── src/
│   └── main.cpp                           # Firmware ESP32 C++ (10 OLED ESL, Parallel HX711, MQTT)
├── web/
│   ├── index.html                         # Giao diện Web Dashboard 10 kệ tạp hóa
│   ├── app.js                             # Logic xử lý MQTT, Chart.js 10 đường và giả lập mua sắm
│   └── style.css                          # Giao diện Glassmorphism Responsive hiện đại
└── platformio.ini                         # Cấu hình PlatformIO build ESP32
```

---

*Dự án thuộc bản quyền Nhóm 22 — Đề tài Kệ Hàng Tạp Hóa Thông Minh 4.0.*
