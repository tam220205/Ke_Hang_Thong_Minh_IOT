# BÁO CÁO ĐỐI CHIẾU TÀI LIỆU VÀ DỰ ÁN

**Chủ đề:** ESP32, 74HC595 và MQTT so với hệ thống Smart Grocery Store 10 kệ hàng  
**Tài liệu tham chiếu:** `esp32-74hc595-mqtt.html`  
**Dự án khảo sát:** firmware, sơ đồ Wokwi, dashboard web, PlatformIO và các flow Node-RED  
**Ngày lập:** 26/11/2026

## 1. Tóm tắt

Tài liệu HTML là bài giảng/lab mẫu về dùng ESP32, hai IC 74HC595 và MQTT để bật/tắt tối đa 16 LED hoặc tín hiệu đầu vào module relay. Trọng tâm gồm nối phần cứng, bitmask, các lệnh JSON `on`, `off`, `clear`, `set`, `setMany`, và firmware subscribe topic điều khiển.

Dự án hiện tại là hệ thống quản lý tồn kho cho 10 kệ hàng. ESP32 đọc cảm biến cân HX711, tính khối lượng và số lượng, cập nhật OLED và LED cảnh báo; dữ liệu được gửi qua MQTT tới dashboard/Node-RED, đồng thời hệ thống cảnh báo và nhận lệnh qua Telegram/MQTT. **Dự án hiện không dùng 74HC595**: các LED cảnh báo nối trực tiếp tới 10 GPIO riêng.

Hai nội dung có thể liên hệ ở kiến trúc IoT và MQTT, nhưng khác mục tiêu phần cứng và giao thức điều khiển. Không nên mô tả dự án hiện tại là đã triển khai bài lab 74HC595/16 kênh.

## 2. Nội dung tài liệu HTML

Tài liệu trình bày các nhóm nội dung sau:

1. **Kiến trúc IoT:** App/API publish lệnh JSON lên MQTT broker; ESP32 subscribe topic để nhận lệnh.
2. **74HC595:** chức năng shift register, chân DS, SHCP, STCP, OE, SRCLR, Q0–Q7 và Q7S; nối chuỗi nhiều IC để tăng số đầu ra với ba GPIO.
3. **Sơ đồ lab:** ESP32 GPIO14/12/13 điều khiển Data/Clock/Latch; hai chip nối tiếp điều khiển 16 LED; hướng dẫn nguồn, điện trở và mass chung.
4. **Bitmask và ánh xạ:** 16 bit tương ứng 16 kênh LED, bit 0–7 ở chip đầu, bit 8–15 ở chip sau.
5. **Điện và an toàn:** ước lượng dòng LED, điện trở nối tiếp, giới hạn tải đầu ra, không kéo cuộn relay trực tiếp từ 74HC595 và dùng driver/nguồn phù hợp.
6. **Firmware mẫu:** cấu hình Wi-Fi/MQTT, ghi shift register, kết nối broker, `setup()`/`loop()`, callback nhận JSON và xử lý lệnh.
7. **Thực hành:** dùng MQTTX/Mosquitto/Python publish lệnh, mô phỏng PlatformIO/Wokwi và bài tập mở rộng.

Ví dụ topic trong tài liệu là `warehouse/{id}/light`; payload dùng trường `action` và `ledIndex`/`mask`/`channels`. Tài liệu viện dẫn repo mẫu `firmware/nodemcu-shelf`, khác cấu trúc dự án đang mở.

## 3. Kiến trúc dự án hiện tại

### 3.1 Firmware ESP32

`src/main.cpp` khai báo 10 kệ `K01`–`K10`, mỗi kệ có thông tin sản phẩm, khối lượng đơn vị, ngưỡng tồn kho, chân DT HX711, chân LED cảnh báo và dữ liệu đo. Firmware:

- Đọc HX711, quy đổi khối lượng và ước tính số lượng theo khối lượng mỗi sản phẩm.
- Bật LED cảnh báo khi số lượng nhỏ hơn hoặc bằng ngưỡng.
- Cập nhật OLED cho từng kệ.
- Gửi telemetry JSON gồm danh sách kệ và tổng số cảnh báo.
- Nhận lệnh MQTT `set`, `tare`, `status`.
- Nhận lệnh Telegram `/status`, `/tare` nếu có secrets; cảnh báo thiếu hàng do Node-RED gửi để tránh trùng tin.
- Lưu tên sản phẩm, khối lượng đơn vị và ngưỡng vào Preferences.

Topic MQTT trong firmware là `nhom22/smartshelf/telemetry` (ESP32 → hệ thống) và `nhom22/smartshelf/cmd` (hệ thống → ESP32). Broker được cấu hình là HiveMQ công cộng.

### 3.2 Phần cứng và mô phỏng

`diagram.json` mô phỏng ESP32 DevKit, 10 HX711, 10 OLED SSD1306 dùng địa chỉ riêng 0x30–0x39 trong Wokwi và 10 LED có điện trở 220 Ω. GPIO18 là clock chung HX711; GPIO21/22 là I²C; mỗi kệ có chân DT và LED riêng. Sơ đồ không có 74HC595. Với phần cứng thật, firmware có chế độ TCA9548A cho OLED cùng địa chỉ.

#### Sơ đồ nối dây tổng quát của dự án hiện tại

```text
ESP32 DevKit                         Các module trên 10 kệ
────────────                         ────────────────────
3V3 ──────────────────────────────── VCC HX711 ×10, OLED ×10
GND ──────────────────────────────── GND HX711 ×10, OLED ×10, LED ×10
GPIO 18 ──────────────────────────── SCK HX711 ×10 (clock chung)
GPIO 21 (SDA) ────────────────────── SDA OLED ×10 (bus I²C chung)
GPIO 22 (SCL) ────────────────────── SCL OLED ×10 (bus I²C chung)

Mỗi kệ i:
  HX711 DT ───────────────────────── GPIO DT riêng của kệ i
  GPIO LED riêng ────────────────── Anode LED
  Cathode LED ── điện trở 220 Ω ─── GND
```

#### Bảng chân từng kệ

| Kệ | HX711 DT | HX711 SCK | OLED I²C address | LED cảnh báo GPIO |
|---|---:|---:|---:|---:|
| K01 | GPIO19 | GPIO18 chung | 0x30 | GPIO2 |
| K02 | GPIO5 | GPIO18 chung | 0x31 | GPIO4 |
| K03 | GPIO23 | GPIO18 chung | 0x32 | GPIO27 |
| K04 | GPIO17 | GPIO18 chung | 0x33 | GPIO14 |
| K05 | GPIO16 | GPIO18 chung | 0x34 | GPIO12 |
| K06 | GPIO32 | GPIO18 chung | 0x35 | GPIO13 |
| K07 | GPIO36 | GPIO18 chung | 0x36 | GPIO15 |
| K08 | GPIO34 | GPIO18 chung | 0x37 | GPIO25 |
| K09 | GPIO35 | GPIO18 chung | 0x38 | GPIO26 |
| K10 | GPIO39 | GPIO18 chung | 0x39 | GPIO33 |

**Lưu ý:** GPIO34–GPIO39 trên ESP32 chỉ dùng làm đầu vào; trong sơ đồ chúng được dùng làm DT của HX711, phù hợp. Các GPIO LED là đầu ra riêng. Địa chỉ OLED trong bảng chỉ dùng cho Wokwi; mạch thật dùng TCA9548A với từng màn hình ở một kênh riêng.

#### Sơ đồ tham khảo 74HC595 trong tài liệu HTML (không phải mạch đang có)

```text
ESP32 GPIO14 (Data)  ── DS   74HC595 số 1
ESP32 GPIO12 (Clock) ── SHCP 74HC595 số 1 và số 2 (nối chung)
ESP32 GPIO13 (Latch) ── STCP 74HC595 số 1 và số 2 (nối chung)
74HC595 số 1 Q7S    ── DS   74HC595 số 2
Hai chip: OE ── GND, SRCLR ── VCC, VCC/GND cấp nguồn chung
Q0–Q7 mỗi chip ── 8 LED, mỗi LED nối tiếp điện trở phù hợp
```

Sơ đồ tham khảo này chỉ tóm tắt cách đấu được mô tả trong HTML. Không nối các GPIO14/12/13 theo phần này vào dự án hiện tại nếu chưa bổ sung phần cứng và firmware điều khiển 74HC595.

### 3.3 Dashboard và dịch vụ

- `web/index.html` và `web/app.js` tạo dashboard 10 kệ, có trạng thái MQTT, biểu đồ, lịch sử trong trình duyệt, xuất CSV, chế độ giả lập và lệnh điều khiển.
- `docker-compose.yml` cấu hình Mosquitto, Node-RED và Nginx dashboard.
- Flow Node-RED nhận telemetry, tổng hợp dữ liệu/mô phỏng một số kệ, hiển thị và gửi lệnh MQTT `tare`, `set`, `status`.
- `platformio.ini` cấu hình ESP32 Arduino và thư viện MQTT, JSON, OLED, Telegram, HX711.

## 4. Bảng đối chiếu

| Hạng mục | Tài liệu HTML | Dự án hiện tại | Kết quả |
|---|---|---|---|
| Mục tiêu | Điều khiển LED/relay nhiều kênh | Giám sát tồn kho 10 kệ | Khác mục tiêu chính |
| IC 74HC595 | Hai IC, 16 đầu ra | Không khai báo hoặc nối trong firmware/sơ đồ | Chưa triển khai |
| LED | LED là đầu ra shift register | 10 LED cảnh báo nối trực tiếp GPIO | Có LED, cách điều khiển khác |
| Cảm biến | Không phải trọng tâm | 10 HX711 đo cân | Chỉ dự án có |
| Màn hình | Không phải trọng tâm | 10 OLED trên I²C | Chỉ dự án có |
| MQTT broker | EMQX/Mosquitto ví dụ | HiveMQ trong firmware, dashboard và Node-RED; Mosquitto Docker là profile tùy chọn | Đã thống nhất mặc định |
| Topic lệnh | `warehouse/{id}/light` | `nhom22/smartshelf/cmd` | Khác topic và mục đích |
| Topic dữ liệu | Status thiết bị tùy chọn | `nhom22/smartshelf/telemetry` | Dự án có telemetry tồn kho |
| Payload | `action`, `ledIndex`, `mask`, `channels` | `cmd`, `id`, `name`, `unit`, `min` | Khác schema, không tương thích trực tiếp |
| Lệnh nhiều kênh | `set` mask hoặc `setMany` | `set` cập nhật cấu hình một kệ; `tare`/`status` | Tên `set` trùng nhưng ngữ nghĩa khác |
| Wi-Fi/Wokwi | Ví dụ dùng Wokwi-GUEST | Firmware cũng dùng Wokwi-GUEST | Tương đồng môi trường mô phỏng |
| Giao diện | MQTTX/CLI/Python ví dụ | Dashboard web và Node-RED | Cùng dùng MQTT, khác ứng dụng |

## 5. Điểm tương đồng có thể trình bày

- Cả hai dùng ESP32, Wi-Fi và MQTT để kết nối thiết bị với ứng dụng/giám sát.
- Cả hai có luồng publish/subscribe, trao đổi JSON và xử lý lệnh từ xa.
- Cả hai có cấu hình Wokwi và thao tác từ giao diện hoặc công cụ bên ngoài.
- Kiến thức broker, topic, payload, callback và vòng lặp MQTT trong tài liệu có thể dùng để giải thích phần truyền thông của dự án.

## 6. Khác biệt và phần chưa được triển khai

1. **Không có 74HC595:** firmware không khai báo SER/SHCP/STCP, không gọi `shiftOut()`, không có `writeShift()` hay bitmask 16-bit. Sơ đồ cũng không có IC này.
2. **Không có điều khiển 16 kênh:** dự án có 10 đèn cảnh báo theo tồn kho, không nhận lệnh bật/tắt tùy ý 16 LED.
3. **Giao thức lệnh khác:** tài liệu dùng `action`; dự án dùng `cmd`. Ví dụ `{"action":"set","mask":255}` không thực hiện lệnh tương ứng trong firmware hiện tại.
4. **MQTT tập trung vào tồn kho:** payload hiện chứa thông tin 10 kệ, không chỉ trạng thái đầu ra.
5. **Phần cứng tải khác:** hướng dẫn relay/LED nối 74HC595 trong tài liệu không thay thế việc kiểm tra mạch hiện tại. Sơ đồ dự án dùng GPIO ESP32 với LED báo và điện trở.

## 7. Các điểm đã xử lý trong lần cập nhật

- Firmware, web dashboard và flow Node-RED thống nhất dùng HiveMQ Public. Mosquitto được đặt trong Docker Compose profile tùy chọn; bật profile không tự chuyển các client sang broker này.
- Mô phỏng giao dịch Node-RED không còn tự chạy theo chu kỳ; người vận hành phải bấm inject. Dashboard đánh dấu dữ liệu demo, và lệnh điều khiển không còn giả lập phản hồi thiết bị ngay lập tức.
- Flow Node-RED nhận ID chuẩn `K01`–`K10`; bộ tách widget K01–K03 đã chuẩn hóa ID firmware trước khi cập nhật widget.
- Firmware chỉ đọc chuỗi HX711 khi cả 10 chân DT báo sẵn sàng; nếu hết thời gian chờ, chu kỳ đó bị bỏ qua.
- Firmware có chế độ TCA9548A cho 10 OLED SSD1306 cùng địa chỉ vật lý `0x3C`. Mô phỏng Wokwi tiếp tục dùng địa chỉ riêng 0x30–0x39.
- Telegram token/chat ID đã được loại khỏi mã nguồn và các flow hiện hành. Firmware dùng `include/secrets.h` bị Git bỏ qua; Node-RED dùng biến môi trường từ `.env`.
- Payload MQTT lệnh vượt 255 byte hiện bị từ chối thay vì bị cắt cụt.

## 8. Việc cần làm thủ công trước khi triển khai

- Thu hồi token Telegram cũ bằng BotFather và tạo token mới: token trước đây đã từng nằm trong mã nguồn/flow. Các file hiện tại đã bỏ token, nhưng bản sao trong lịch sử Git vẫn có thể chứa thông tin cũ.
- Sao chép `include/secrets.example.h` thành `include/secrets.h`, cấu hình token mới/chat IDs nếu cần Telegram trực tiếp từ ESP32. Sao chép `.env.example` thành `.env` cho Node-RED.
- Khi lắp mạch thật, dùng TCA9548A cho 10 OLED SSD1306 có địa chỉ trùng nhau và đặt `OLED_USE_TCA9548A 1` trong `include/secrets.h`.
- Hiệu chuẩn HX711, kiểm tra đủ 10 cảm biến sẵn sàng, nguồn và dây thực tế trước khi vận hành. Những thay đổi lần này chưa được biên dịch/chạy thử trên phần cứng.

Các mục trên là nhận xét từ mã nguồn và cấu hình; báo cáo này không khẳng định dự án đã được biên dịch hoặc kiểm thử trên phần cứng.

## 9. Kết luận

Dự án hiện tại triển khai hệ thống Smart Shelf 10 kệ với ESP32, HX711, OLED, LED cảnh báo, MQTT, Node-RED/dashboard và Telegram. Tài liệu HTML cung cấp kiến thức nền phù hợp về MQTT và điều khiển đầu ra số bằng ESP32, nhưng phần 74HC595, bitmask 16 kênh và payload `action` mới là nội dung bài giảng, chưa xuất hiện trong sản phẩm hiện tại.

Khi thuyết trình, nên giới thiệu HTML là **tài liệu tham khảo về hướng mở rộng điều khiển đầu ra bằng 74HC595**. Sau đó mô tả riêng hệ thống hiện tại dùng GPIO trực tiếp cho LED cảnh báo và MQTT để giám sát/điều khiển cấu hình tồn kho.

## 10. Tệp đã đối chiếu

- `esp32-74hc595-mqtt.html` — slide tham khảo.
- `src/main.cpp` — firmware ESP32.
- `platformio.ini`, `wokwi.toml`, `diagram.json` — cấu hình build và mô phỏng phần cứng.
- `web/index.html`, `web/app.js`, `web/style.css` — dashboard.
- `docker-compose.yml`, `nodered_flow.json`, `combined_flows.json`, `docker/nodered/data/flows.json` — dịch vụ và xử lý luồng.
- `README.md` và các tài liệu Markdown trong thư mục gốc — mô tả dự án.
