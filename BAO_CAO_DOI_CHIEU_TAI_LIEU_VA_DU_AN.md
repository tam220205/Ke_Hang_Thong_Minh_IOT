# BÁO CÁO ĐỐI CHIẾU TÀI LIỆU VÀ DỰ ÁN

**Chủ đề:** ESP32, 74HC595 và MQTT so với hệ thống Smart Grocery Store 10 kệ hàng  
**Tài liệu tham chiếu:** `esp32-74hc595-mqtt.html`  
**Dự án khảo sát:** firmware, sơ đồ Wokwi, dashboard web, PlatformIO và các flow Node-RED  
**Phiên bản mã nguồn:** nhánh `fix/smartshelf-reliability`, commit `0cebea0` ("fix: harden smart shelf telemetry and configuration"), pull ngày 27/11/2026  
**Ngày lập:** 27/11/2026 (thay cho bản ngày 26/11/2026, vốn đối chiếu với commit `74ba60d`)

## 1. Tóm tắt

Tài liệu HTML là bài giảng/lab mẫu về dùng ESP32, hai IC 74HC595 và MQTT để bật/tắt tối đa 16 LED hoặc tín hiệu đầu vào module relay. Trọng tâm gồm nối phần cứng, bitmask, các lệnh JSON `on`, `off`, `clear`, `set`, `setMany`, và firmware subscribe topic điều khiển.

Dự án là hệ thống quản lý tồn kho cho 10 kệ hàng. ESP32 đọc cảm biến cân HX711, tính khối lượng và số lượng, cập nhật OLED và LED cảnh báo, gửi dữ liệu qua MQTT tới dashboard và Node-RED; Node-RED gửi cảnh báo thiếu hàng qua Telegram. **Dự án không dùng 74HC595**: các LED cảnh báo nối trực tiếp tới 10 GPIO riêng.

Hai nội dung liên hệ được ở kiến trúc IoT và MQTT, nhưng khác mục tiêu phần cứng và giao thức điều khiển. Không nên mô tả dự án là đã triển khai bài lab 74HC595/16 kênh.

So với bản ngày 26/11, commit `0cebea0` đã xử lý phần lớn các điểm cần rà soát (broker, mã kệ, đọc HX711, OLED trên phần cứng thật, thông tin xác thực, payload lệnh). Tuy vậy, kiểm chứng ngày 27/11 cho thấy **còn ba lỗi khiến luồng dữ liệu và cảnh báo không chạy**:

1. Bản tin telemetry 10 kệ dài hơn bộ đệm MQTT, nên ESP32 không gửi được dữ liệu (mục 8.1).
2. Node xử lý trung tâm của Node-RED bị lỗi cú pháp, phát sinh trong chính commit `0cebea0` (mục 8.2).
3. Tin cảnh báo MarkdownV2 còn ký tự chưa escape, nên Telegram từ chối tin (mục 8.3).

Vì firmware đã bỏ việc tự gửi cảnh báo và giao cho Node-RED, ở phiên bản này **không còn đường nào gửi được cảnh báo thiếu hàng qua Telegram**.

## 2. Nội dung tài liệu HTML

Tài liệu trình bày các nhóm nội dung sau:

1. **Kiến trúc IoT:** App/API publish lệnh JSON lên MQTT broker; ESP32 subscribe topic để nhận lệnh.
2. **74HC595:** chức năng shift register, chân DS, SHCP, STCP, OE, SRCLR, Q0–Q7 và Q7S; nối chuỗi nhiều IC để tăng số đầu ra với ba GPIO.
3. **Sơ đồ lab:** ESP32 GPIO14/12/13 điều khiển Data/Clock/Latch; hai chip nối tiếp điều khiển 16 LED; hướng dẫn nguồn, điện trở và mass chung.
4. **Bitmask và ánh xạ:** 16 bit tương ứng 16 kênh LED, bit 0–7 ở chip đầu, bit 8–15 ở chip sau.
5. **Điện và an toàn:** ước lượng dòng LED, điện trở nối tiếp, giới hạn tải đầu ra, không kéo cuộn relay trực tiếp từ 74HC595 và dùng driver/nguồn phù hợp.
6. **Firmware mẫu:** cấu hình Wi-Fi/MQTT, ghi shift register, kết nối broker, `setup()`/`loop()`, callback nhận JSON và xử lý lệnh.
7. **Thực hành:** dùng MQTTX/Mosquitto/Python publish lệnh, mô phỏng PlatformIO/Wokwi và bài tập mở rộng.

Ví dụ topic trong tài liệu là `warehouse/{id}/light`; payload dùng trường `action` và `ledIndex`/`mask`/`channels`. Tài liệu viện dẫn repo mẫu `firmware/nodemcu-shelf`, khác cấu trúc dự án.

## 3. Kiến trúc dự án (commit `0cebea0`)

### 3.1 Firmware ESP32

`src/main.cpp` khai báo 10 kệ `K01`–`K10`, mỗi kệ có tên sản phẩm, khối lượng đơn vị, ngưỡng tồn kho, chân DT HX711, chân LED cảnh báo và dữ liệu đo. Firmware:

- Đọc 10 HX711 dùng chung chân clock. Trước khi phát xung, firmware chờ tối đa 35 ms cho cả 10 chân DT báo sẵn sàng; nếu hết thời gian, chu kỳ đó bị bỏ qua.
- Quy đổi khối lượng, ước tính số lượng theo khối lượng mỗi sản phẩm và bật LED cảnh báo khi số lượng nhỏ hơn hoặc bằng ngưỡng.
- Cập nhật 10 OLED. Mặc định mỗi màn hình có địa chỉ riêng 0x30–0x39 như trong Wokwi; khi đặt `OLED_USE_TCA9548A 1` trong `include/secrets.h`, firmware chọn từng kênh của bộ ghép kênh TCA9548A (địa chỉ 0x70) cho 10 OLED cùng địa chỉ 0x3C.
- Gửi telemetry JSON mỗi 2,5 giây, gồm thông tin thiết bị, trường `source: "hardware"`, danh sách 10 kệ và tổng số cảnh báo.
- Nhận lệnh MQTT `set`, `tare`, `status`. Payload từ 256 byte trở lên bị từ chối và ghi log, không còn bị cắt cụt.
- Nhận lệnh Telegram `/start`, `/help`, `/status`, `/tare Kxx` và gửi tin khởi động, **chỉ khi đã cấu hình token và chat ID**. Firmware không còn gửi cảnh báo thiếu hàng; việc này giao cho Node-RED để tránh trùng tin.
- Đọc token và chat ID Telegram từ `include/secrets.h` (được Git bỏ qua); nếu không có tệp này, firmware dùng `include/secrets.example.h` với giá trị rỗng và tắt Telegram.
- Lưu tên sản phẩm, khối lượng đơn vị và ngưỡng vào Preferences.

Topic MQTT là `nhom22/smartshelf/telemetry` (ESP32 → hệ thống) và `nhom22/smartshelf/cmd` (hệ thống → ESP32). Broker là HiveMQ công cộng (`broker.hivemq.com:1883`).

### 3.2 Phần cứng và mô phỏng

`diagram.json` mô phỏng ESP32 DevKit, 10 HX711, 10 OLED SSD1306 dùng địa chỉ riêng 0x30–0x39 và 10 LED có điện trở 220 Ω. GPIO18 là clock chung HX711; GPIO21/22 là I²C; mỗi kệ có chân DT và LED riêng. Sơ đồ không có 74HC595 và không có TCA9548A (bộ ghép kênh chỉ dùng cho mạch thật).

#### Sơ đồ nối dây tổng quát

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

Với mạch thật dùng TCA9548A: SDA/SCL của ESP32 nối vào TCA9548A, mỗi OLED nối vào một kênh SDx/SCx riêng (kênh 0 cho K01 đến kênh 9 cho K10). TCA9548A chỉ có 8 kênh, nên 10 màn hình cần hai bộ ghép kênh ở hai địa chỉ khác nhau; firmware hiện chỉ điều khiển một bộ tại 0x70 và ghi `1u << channel` với channel 0–9, nên **kênh 8 và 9 (K09, K10) chưa được hỗ trợ**. Cần sửa firmware trước khi lắp 10 màn hình thật.

#### Bảng chân từng kệ

| Kệ | HX711 DT | HX711 SCK | OLED I²C (Wokwi) | LED cảnh báo GPIO |
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

**Lưu ý:** GPIO34–GPIO39 chỉ dùng làm đầu vào; sơ đồ dùng chúng làm DT của HX711, phù hợp. Địa chỉ OLED trong bảng chỉ dùng cho Wokwi. LED của K01, K05, K07 nằm trên các chân khởi động (strapping) GPIO2, GPIO12, GPIO15; trên mạch thật cần kiểm tra mạch LED không kéo các chân này lên mức cao lúc cấp nguồn (GPIO12 ở mức cao khi khởi động làm ESP32 chọn điện áp flash 1,8 V).

#### Sơ đồ tham khảo 74HC595 trong tài liệu HTML (không phải mạch đang có)

```text
ESP32 GPIO14 (Data)  ── DS   74HC595 số 1
ESP32 GPIO12 (Clock) ── SHCP 74HC595 số 1 và số 2 (nối chung)
ESP32 GPIO13 (Latch) ── STCP 74HC595 số 1 và số 2 (nối chung)
74HC595 số 1 Q7S    ── DS   74HC595 số 2
Hai chip: OE ── GND, SRCLR ── VCC, VCC/GND cấp nguồn chung
Q0–Q7 mỗi chip ── 8 LED, mỗi LED nối tiếp điện trở phù hợp
```

Sơ đồ này chỉ tóm tắt cách đấu trong HTML. GPIO14/12/13 đang là chân LED của K04, K05, K06, nên không nối theo phần này vào dự án nếu chưa đổi chân và bổ sung firmware điều khiển 74HC595.

### 3.3 Dashboard và dịch vụ

- `web/index.html` và `web/app.js`: dashboard 10 kệ kết nối HiveMQ qua WebSocket bảo mật (`wss://broker.hivemq.com:8884/mqtt`), có biểu đồ, lịch sử lưu trong trình duyệt, xuất CSV, chế độ giả lập và gửi lệnh. Dashboard hiển thị nguồn dữ liệu (thiết bị thật hay giả lập) và cảnh báo khi ESP32 không gửi telemetry quá 8 giây. Lệnh chỉ được gửi khi đang kết nối MQTT, không còn giả lập phản hồi.
- `docker-compose.yml`: Node-RED (cổng 1880, nhận `TELEGRAM_BOT_TOKEN`, `TELEGRAM_CHAT_IDS` từ `.env`) và Nginx phục vụ dashboard (cổng 8088). Mosquitto chỉ chạy khi bật profile `local-broker`; bật profile này không tự chuyển firmware, dashboard và flow sang broker cục bộ.
- Flow Node-RED (`docker/nodered/data/flows.json`, `nodered_flow.json`, `combined_flows.json`): nhận telemetry từ HiveMQ, hợp nhất trạng thái 10 kệ, hiển thị bảng, thống kê, kiểm tra ngưỡng chống spam (nhắc lại sau 5 phút), gửi cảnh báo Telegram MarkdownV2, và có các nút inject gửi lệnh `tare`, `set`, `status`. Mô phỏng giao dịch K04–K10 chỉ chạy khi bấm inject.
- `platformio.ini`: ESP32 Arduino với HX711, PubSubClient, SSD1306/GFX, ArduinoJson 7, UniversalTelegramBot.

## 4. Bảng đối chiếu

| Hạng mục | Tài liệu HTML | Dự án (commit `0cebea0`) | Kết quả |
|---|---|---|---|
| Mục tiêu | Điều khiển LED/relay nhiều kênh | Giám sát tồn kho 10 kệ | Khác mục tiêu chính |
| IC 74HC595 | Hai IC, 16 đầu ra | Không khai báo hoặc nối trong firmware/sơ đồ | Chưa triển khai |
| LED | LED là đầu ra shift register | 10 LED cảnh báo nối trực tiếp GPIO | Có LED, cách điều khiển khác |
| Cảm biến | Không phải trọng tâm | 10 HX711 đo cân | Chỉ dự án có |
| Màn hình | Không phải trọng tâm | 10 OLED I²C; chế độ TCA9548A cho mạch thật | Chỉ dự án có |
| MQTT broker | EMQX/Mosquitto ví dụ | HiveMQ ở firmware, dashboard và Node-RED; Mosquitto là profile tùy chọn | Đã thống nhất |
| Topic lệnh | `warehouse/{id}/light` | `nhom22/smartshelf/cmd` | Khác topic và mục đích |
| Topic dữ liệu | Status thiết bị tùy chọn | `nhom22/smartshelf/telemetry` | Dự án có telemetry tồn kho |
| Payload | `action`, `ledIndex`, `mask`, `channels` | `cmd`, `id`, `name`, `unit`, `min` | Khác schema, không tương thích trực tiếp |
| Lệnh nhiều kênh | `set` mask hoặc `setMany` | `set` cập nhật cấu hình một kệ; `tare`/`status` | Tên `set` trùng nhưng ngữ nghĩa khác |
| Cảnh báo | Không có | Node-RED gửi Telegram; firmware chỉ trả lời lệnh | Dự án có, nhưng hiện không chạy (mục 8) |
| Wi-Fi/Wokwi | Ví dụ dùng Wokwi-GUEST | Firmware cũng dùng Wokwi-GUEST | Tương đồng môi trường mô phỏng |
| Giao diện | MQTTX/CLI/Python ví dụ | Dashboard web và Node-RED | Cùng dùng MQTT, khác ứng dụng |

## 5. Điểm tương đồng có thể trình bày

- Cả hai dùng ESP32, Wi-Fi và MQTT để kết nối thiết bị với ứng dụng giám sát.
- Cả hai có luồng publish/subscribe, trao đổi JSON và xử lý lệnh từ xa.
- Cả hai có cấu hình Wokwi và thao tác từ giao diện hoặc công cụ bên ngoài.
- Kiến thức broker, topic, payload, callback và vòng lặp MQTT trong tài liệu dùng được để giải thích phần truyền thông của dự án.

## 6. Khác biệt và phần chưa được triển khai

1. **Không có 74HC595:** firmware không khai báo SER/SHCP/STCP, không gọi `shiftOut()`, không có bitmask 16-bit. Sơ đồ cũng không có IC này.
2. **Không có điều khiển 16 kênh:** dự án có 10 đèn cảnh báo theo tồn kho, không nhận lệnh bật/tắt tùy ý từng LED.
3. **Giao thức lệnh khác:** tài liệu dùng `action`; dự án dùng `cmd`. Ví dụ `{"action":"set","mask":255}` không có tác dụng với firmware.
4. **MQTT tập trung vào tồn kho:** payload chứa thông tin 10 kệ, không chỉ trạng thái đầu ra.
5. **Phần cứng tải khác:** hướng dẫn relay/LED nối 74HC595 trong tài liệu không thay thế việc kiểm tra mạch hiện tại.

## 7. Thay đổi so với bản báo cáo ngày 26/11

| Điểm cần rà soát (bản 26/11) | Trạng thái ở commit `0cebea0` |
|---|---|
| Broker không đồng nhất (HiveMQ trong firmware, Mosquitto trong Docker) | Đã xử lý: firmware, dashboard, ba file flow cùng dùng HiveMQ; Mosquitto là profile tùy chọn |
| Phân biệt dữ liệu thật và mô phỏng | Đã xử lý: telemetry có trường `source`; dashboard ghi rõ nguồn; mô phỏng Node-RED chỉ chạy khi bấm |
| Mã kệ `K1`–`K3` và `K01`–`K10` | Phần lớn đã xử lý: flow tách widget theo `K01`–`K03`; dashboard chuẩn hóa `K1` → `K01`. Các nút inject trong Node-RED vẫn gửi `K1`–`K3` (firmware vẫn hiểu) |
| HX711 chỉ chờ cảm biến đầu tiên | Đã xử lý: chờ cả 10 chân DT, quá 35 ms thì bỏ mẫu |
| Nhiều OLED trên cùng bus | Đã xử lý một phần: có chế độ TCA9548A, nhưng chỉ đủ 8 kênh cho 10 màn hình (mục 3.2) |
| Token Telegram ghi cứng | Đã xử lý trong mã hiện hành: firmware dùng `secrets.h`, Node-RED dùng biến môi trường. Token cũ vẫn còn trong lịch sử Git |
| Payload lệnh bị cắt ở 255 byte | Đã xử lý: payload từ 256 byte bị từ chối và ghi log |

## 8. Lỗi còn tồn tại (kiểm chứng ngày 27/11/2026)

> **Cập nhật:** các lỗi ở mục 8.1–8.4 và lỗi TCA9548A ở mục 3.2 đã được sửa và kiểm thử trong thư mục làm việc (chưa commit). Chi tiết ở `BAO_CAO_SUA_LOI_HE_THONG.md`. Mục 8.5 và 8.6 chưa sửa.

### 8.1 Telemetry dài hơn bộ đệm MQTT, ESP32 không gửi được dữ liệu (nghiêm trọng)

- Bản tin telemetry 10 kệ với tên sản phẩm mặc định dài khoảng **1.047–1.097 byte** (tùy giá trị khối lượng, số lượng, uptime), và dài hơn nữa nếu đổi tên sản phẩm bằng lệnh `set`.
- `publishTelemetry()` ghi JSON vào `char buffer[1024]` (`src/main.cpp:307`), nên chuỗi bị cắt còn 1.023 byte và không còn là JSON hợp lệ.
- PubSubClient được đặt `setBufferSize(1024)` (`src/main.cpp:523`). Hàm `publish()` của thư viện từ chối gửi khi `5 + 2 + độ dài topic + độ dài payload` vượt bộ đệm; với topic 27 byte, payload tối đa chỉ **990 byte**.
- Hệ quả: `mqttClient.publish()` (`src/main.cpp:310`) luôn trả về `false`, và firmware không ghi log khi thất bại. Dashboard và Node-RED không nhận được dữ liệu thật; dashboard chỉ báo "ESP32 chưa gửi telemetry trong hơn 8 giây".
- Cách sửa: tính kích thước bằng `measureJson(doc)`, tăng `setBufferSize()` (ví dụ 2048) và bộ đệm serialize tương ứng, ghi log khi `publish()` thất bại; hoặc rút gọn payload.

### 8.2 Node "🛒 10-Shelf Grocery Engine" của Node-RED lỗi cú pháp (nghiêm trọng, phát sinh trong `0cebea0`)

- Trong mã của node này, đoạn khai báo `device: ...,` và `source: ...` bị nối bằng hai ký tự `\` và `n` nằm ngoài chuỗi, thay vì một dấu xuống dòng thật. JavaScript báo lỗi `Invalid or unexpected token`, nên node không chạy được.
- Lỗi có ở cả ba file `docker/nodered/data/flows.json`, `nodered_flow.json`, `combined_flows.json`. Ở commit gốc `74ba60d`, cả ba file đều không có lỗi cú pháp.
- Node này nhận mọi telemetry từ MQTT và nút mô phỏng, rồi chuyển tiếp tới lưu context, thống kê, bảng 10 kệ và kiểm tra ngưỡng/Telegram. Khi node lỗi, **toàn bộ phần xử lý phía sau không chạy**; chỉ còn node debug "Raw JSON" nhận được dữ liệu.
- Cùng node có một dòng chú thích bị dán dính hai câu ("...on a timer.n mô phỏng khách..."). Dòng này vô hại nhưng nên sửa cho rõ.
- Cách sửa: thay `\n` thừa bằng xuống dòng thật trong cả ba file, rồi nạp lại flow.

### 8.3 Tin cảnh báo MarkdownV2 còn ký tự chưa escape (nghiêm trọng)

- Hàm `escMD()` chỉ được áp dụng cho dữ liệu động. Phần chữ cố định vẫn chứa ký tự dành riêng của MarkdownV2 chưa escape: `!` (viết dạng `!`), `[` và `]` quanh thanh tiến độ, `+` và `-` trong "ESP32 + Node-RED". Trong chuỗi JavaScript, `'\+'` và `'\-'` chỉ cho ra `+` và `-`, nên dấu gạch chéo bị mất.
- Chạy thử hàm định dạng với dữ liệu mẫu: `flows.json` và `nodered_flow.json` có 6 ký tự chưa escape; `combined_flows.json` có 4. Telegram trả lỗi 400 ("can't parse entities") và không gửi tin.
- Cách sửa: viết `'\\!'`, `'\\['`, `'\\]'`, `'\\+'`, `'\\-'` trong phần chữ cố định, hoặc đưa toàn bộ chữ cố định qua `escMD()` trước khi thêm định dạng.

### 8.4 Dashboard chèn dữ liệu MQTT bằng `innerHTML` (trung bình)

- Tên và nhóm sản phẩm lấy từ telemetry được chèn thẳng vào HTML bằng `innerHTML`, ví dụ trong `generateAnalyticsInsights()` (`web/app.js:475`), thẻ kệ (`web/app.js:232`), nút điều khiển và danh sách chọn kệ (`web/app.js:586–608`).
- Broker HiveMQ công cộng không xác thực, nên bất kỳ ai cũng publish được lên `nhom22/smartshelf/telemetry` một tên sản phẩm chứa mã HTML/JavaScript, và mã đó sẽ chạy trong trình duyệt đang mở dashboard.
- Cách sửa: escape HTML (hàm `escapeHtml`) hoặc dùng `textContent` cho mọi giá trị nhận từ MQTT.

### 8.5 Broker công cộng không xác thực (cần nêu khi trình bày)

Bất kỳ ai biết topic `nhom22/smartshelf/cmd` đều gửi được lệnh `set`/`tare` tới ESP32 và gửi telemetry giả tới dashboard/Node-RED. Cách này chấp nhận được cho bài thực hành và mô phỏng, nhưng triển khai thật cần broker riêng có tài khoản/mật khẩu và TLS.

### 8.6 Điểm nhỏ

- Tên nút inject và nhãn trong Node-RED ghi Kệ 2 là "BanhOishi", Kệ 3 là "MiHaoHao", ngược với firmware (K02 là Mì Hảo Hảo, K03 là Bánh Oishi).
- Lệnh `tare` qua MQTT không cập nhật LED ngay (LED đổi ở chu kỳ đọc sau, tối đa 2,5 giây), trong khi `/tare` qua Telegram cập nhật ngay.
- Firmware trả lời Telegram bằng Markdown (bản cũ) và chèn tên sản phẩm không escape; tên chứa `_` hoặc `*` có thể làm tin `/status` bị từ chối.

## 9. Việc cần làm thủ công trước khi triển khai

- Thu hồi token Telegram cũ bằng BotFather và tạo token mới: token trước đây từng nằm trong mã nguồn/flow, và vẫn còn trong lịch sử Git trên GitHub.
- Sao chép `include/secrets.example.h` thành `include/secrets.h` và điền token mới/chat ID nếu cần Telegram trực tiếp từ ESP32. Sao chép `.env.example` thành `.env` cho Node-RED.
- Khi lắp mạch thật, dùng TCA9548A cho các OLED cùng địa chỉ và đặt `OLED_USE_TCA9548A 1` trong `include/secrets.h` (sau khi sửa firmware để hỗ trợ đủ 10 kênh, mục 3.2).
- Hiệu chuẩn HX711, kiểm tra đủ 10 cảm biến sẵn sàng, nguồn và dây thực tế trước khi vận hành.

## 10. Kiểm chứng đã thực hiện

| Nội dung | Cách kiểm | Kết quả |
|---|---|---|
| Biên dịch firmware | `pio run` (chế độ mặc định, OLED Wokwi) | Thành công; RAM 15,3%, Flash 80,2% |
| Kích thước telemetry | Dựng lại JSON đúng cấu trúc firmware với tên mặc định | 1.047–1.097 byte > 990 byte cho phép |
| Giới hạn PubSubClient | Đọc mã `PubSubClient::publish()` bản 2.8 | Từ chối khi vượt `bufferSize`, đúng như mục 8.1 |
| Cú pháp hàm Node-RED | Biên dịch mọi node `function` của ba file flow bằng Node.js | 1 lỗi mỗi file (node Engine); commit `74ba60d`: 0 lỗi |
| Escape MarkdownV2 | Chạy hàm định dạng với dữ liệu mẫu và dò ký tự dành riêng | 6 / 6 / 4 ký tự chưa escape |

Chưa thực hiện: chạy mô phỏng Wokwi, chạy container Docker, gửi tin Telegram thật và thử trên phần cứng.

## 11. Kết luận

Dự án triển khai hệ thống Smart Shelf 10 kệ với ESP32, HX711, OLED, LED cảnh báo, MQTT, Node-RED/dashboard và Telegram. Tài liệu HTML cung cấp kiến thức nền về MQTT và điều khiển đầu ra số bằng ESP32; phần 74HC595, bitmask 16 kênh và payload `action` chỉ là nội dung bài giảng, chưa có trong sản phẩm.

Commit `0cebea0` đã khắc phục phần lớn các điểm rà soát của bản báo cáo trước. Tuy nhiên, trước khi trình bày hoặc bàn giao cần sửa ba lỗi ở mục 8.1–8.3; nếu không, ESP32 không gửi được telemetry, Node-RED không xử lý dữ liệu và Telegram không nhận được cảnh báo.

Khi thuyết trình, nên giới thiệu HTML là **tài liệu tham khảo về hướng mở rộng điều khiển đầu ra bằng 74HC595**, sau đó mô tả riêng hệ thống hiện tại dùng GPIO trực tiếp cho LED cảnh báo và MQTT để giám sát và điều khiển cấu hình tồn kho.

## 12. Tệp đã đối chiếu

- `esp32-74hc595-mqtt.html`: slide tham khảo.
- `src/main.cpp`, `include/secrets.example.h`: firmware ESP32.
- `platformio.ini`, `wokwi.toml`, `diagram.json`: cấu hình build và mô phỏng phần cứng.
- `web/index.html`, `web/app.js`: dashboard.
- `docker-compose.yml`, `.env.example`, `nodered_flow.json`, `combined_flows.json`, `docker/nodered/data/flows.json`: dịch vụ và xử lý luồng.
- `README.md` và các tài liệu Markdown trong thư mục gốc: mô tả dự án.
