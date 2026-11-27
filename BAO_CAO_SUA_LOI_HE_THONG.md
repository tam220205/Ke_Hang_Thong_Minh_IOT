# BÁO CÁO SỬA LỖI HỆ THỐNG SMART SHELF 10 KỆ

**Dự án:** Smart Grocery Store 10 kệ hàng (ESP32, HX711, OLED, MQTT, Node-RED, dashboard web, Telegram)  
**Mã nguồn gốc:** nhánh `fix/smartshelf-reliability`, commit `0cebea0`  
**Báo cáo liên quan:** `BAO_CAO_DOI_CHIEU_TAI_LIEU_VA_DU_AN.md` (mục 8 liệt kê các lỗi được sửa ở đây)  
**Ngày lập:** 27/11/2026  
**Trạng thái:** đã sửa trong thư mục làm việc, **chưa commit và chưa push**

## 1. Tóm tắt

Commit `0cebea0` còn 5 lỗi. Ba lỗi đầu làm hỏng toàn bộ luồng dữ liệu: ESP32 không gửi được telemetry, Node-RED không xử lý được dữ liệu, và Telegram không nhận cảnh báo nào. Cả 5 lỗi đã được sửa và kiểm thử lại. Mỗi phép thử đều chạy trên mã trước và sau khi sửa để thấy rõ khác biệt.

| # | Lỗi | Mức độ | Thành phần | Kết quả |
|---|---|---|---|---|
| 1 | Telemetry dài hơn bộ đệm MQTT, ESP32 không gửi được | Nghiêm trọng | Firmware | Đã sửa |
| 2 | Node "10-Shelf Grocery Engine" lỗi cú pháp | Nghiêm trọng | Node-RED (3 file flow) | Đã sửa |
| 3 | Tin cảnh báo MarkdownV2 có ký tự chưa escape | Nghiêm trọng | Node-RED (3 file flow) | Đã sửa |
| 4 | Chế độ TCA9548A không điều khiển được K09, K10 | Trung bình (chỉ mạch thật) | Firmware, tài liệu | Đã sửa |
| 5 | Dashboard chèn dữ liệu MQTT bằng `innerHTML` | Trung bình (bảo mật) | Web | Đã sửa |

## 2. Chi tiết từng lỗi

### 2.1 Telemetry dài hơn bộ đệm MQTT

**Hiện tượng.** Dashboard và Node-RED không nhận được dữ liệu thật từ ESP32. Dashboard chỉ báo "ESP32 chưa gửi telemetry trong hơn 8 giây", còn Serial Monitor không có dòng lỗi nào.

**Nguyên nhân.**
- Bản tin 10 kệ dài 1.042–1.108 byte, tùy giá trị khối lượng, số lượng và uptime.
- Firmware ghi JSON vào `char buffer[1024]`, nên chuỗi bị cắt còn 1.023 byte và không còn là JSON hợp lệ.
- PubSubClient được đặt `setBufferSize(1024)`. Hàm `publish()` từ chối khi `5 + 2 + độ dài topic + độ dài nội dung` vượt bộ đệm; với topic `nhom22/smartshelf/telemetry` (27 byte), nội dung tối đa chỉ là 990 byte.
- Khi gửi thất bại, firmware không ghi log.

**Cách sửa** (`src/main.cpp`):
- Thêm hằng `MQTT_BUFFER_SIZE = 2048` và `JSON_BUFFER_SIZE = 1536`.
- `publishTelemetry()` tính trước độ dài bằng `measureJson()`. Nếu bản tin quá dài, firmware ghi log và bỏ qua lần gửi đó thay vì gửi JSON bị cắt.
- Bộ đệm serialize chuyển thành `static` để không chiếm 1,5 KB stack.
- Ghi log khi `publish()` thất bại, kèm độ dài bản tin và kích thước bộ đệm.
- Kiểm tra giá trị trả về của `setBufferSize()` và ghi log nếu không cấp phát được.

**Kiểm chứng.**

| | Trước khi sửa | Sau khi sửa |
|---|---|---|
| Nội dung tối đa PubSubClient cho phép | 990 byte | 2.014 byte |
| Bản tin 10 kệ (tên mặc định) | 1.042–1.108 byte, bị từ chối | 1.042–1.108 byte, gửi được |

### 2.2 Node "🛒 10-Shelf Grocery Engine" lỗi cú pháp

**Hiện tượng.** Node-RED báo lỗi khi nạp flow. Mọi node phía sau Engine (lưu context, thống kê, bảng 10 kệ, kiểm tra ngưỡng, gửi Telegram) không bao giờ nhận được dữ liệu.

**Nguyên nhân.** Lỗi phát sinh trong commit `0cebea0`. Khi thêm trường `source`, dấu xuống dòng giữa `device: ...,` và `source: ...` bị ghi thành hai ký tự `\` và `n` nằm ngoài chuỗi. JavaScript báo `Invalid or unexpected token`. Lỗi có ở cả ba file `docker/nodered/data/flows.json`, `nodered_flow.json` và `combined_flows.json`.

**Cách sửa.** Thay hai ký tự đó bằng xuống dòng thật. Sửa luôn dòng chú thích bị dán dính hai câu ("...on a timer.n mô phỏng khách..."). Script sửa thay trực tiếp trên văn bản gốc và kiểm tra mỗi chỗ thay xuất hiện đúng một lần, nên định dạng file được giữ nguyên (mỗi file chỉ đổi 2 dòng).

### 2.3 Tin cảnh báo MarkdownV2 có ký tự chưa escape

**Hiện tượng.** Telegram trả lỗi 400 ("can't parse entities") và không gửi cảnh báo.

**Nguyên nhân.** MarkdownV2 bắt buộc escape các ký tự `_ * [ ] ( ) ~ \` > # + - = | { } . !` khi chúng không dùng để định dạng.
- `flows.json` và `nodered_flow.json`: hàm `escMD()` chỉ áp dụng cho dữ liệu động. Phần chữ cố định còn 6 ký tự chưa escape: hai dấu `!` (viết dạng `\u0021`, vẫn ra `!`), `[` và `]` quanh thanh tiến độ, `+` và `-` trong "ESP32 + Node-RED". Trong chuỗi JavaScript, `'\+'` và `'\-'` chỉ cho ra `+` và `-`.
- `combined_flows.json`: hàm định dạng không escape gì. Tiêu đề, dòng kết, cùng tên sản phẩm, ngoặc đơn, dấu chấm và dấu gạch ngang trong từng dòng cảnh báo đều chưa escape.

**Cách sửa.**
- `flows.json`, `nodered_flow.json`: viết lại phần chữ cố định với `\\!`, `\\[`, `\\]`, `\\+`, `\\-`.
- `combined_flows.json`: viết lại hàm định dạng. Hàm thêm `escMD()`, dựng từng dòng cảnh báo từ `msg.alertShelves` và đưa mọi đoạn chữ, cố định lẫn động, qua `escMD()` trước khi thêm dấu định dạng `*`, `_`.

**Kiểm chứng.** Chạy cả chuỗi xử lý bằng chính mã trong file flow: telemetry → Engine → Kiểm tra ngưỡng → Định dạng Telegram. Tên sản phẩm thử có chứa ký tự khó: `Coca-Cola 330ml (lon).` và `Mi Hao_Hao [tom]`.

| File | Trước khi sửa | Sau khi sửa |
|---|---|---|
| `docker/nodered/data/flows.json` | Engine lỗi cú pháp, chuỗi dừng ngay | Engine đủ 10 kệ, 2 kệ cảnh báo, chống spam hoạt động, 0 ký tự chưa escape, dấu định dạng cân cặp |
| `nodered_flow.json` | Như trên | Như trên |
| `combined_flows.json` | Như trên | Như trên |

### 2.4 Chế độ TCA9548A không điều khiển được K09, K10

**Hiện tượng.** Trên mạch thật bật `OLED_USE_TCA9548A 1`, màn hình K09 và K10 không hiển thị.

**Nguyên nhân.** TCA9548A chỉ có 8 kênh. Firmware dùng một bộ duy nhất tại 0x70 và ghi `1u << channel` với channel 0–9. Với channel 8 và 9, giá trị 256 và 512 bị cắt về 0 khi ghi một byte, nên không kênh nào được chọn mà hàm vẫn báo thành công. Tài liệu cũng ghi sai là "10 kênh TCA9548A" và "chọn kênh 0–9".

**Cách sửa.**
- Firmware dùng hai bộ TCA9548A: 0x70 cho K01–K08 (kênh 0–7) và 0x71 cho K09–K10 (kênh 0–1, chân A0 của bộ thứ hai nối VCC).
- Mỗi lần chọn màn hình, firmware đóng mọi kênh của bộ còn lại, để không có hai OLED cùng địa chỉ 0x3C trên bus cùng lúc. Hàm trả lỗi nếu không ghi được tới bộ đang chọn.
- Cập nhật hướng dẫn nối dây trong `README.md`, `HE_THONG_IOT_VA_DANH_SACH_THIET_BI.md` và chú thích trong `include/secrets.example.h`.

**Kiểm chứng.** Biên dịch thành công khi `OLED_USE_TCA9548A 1` (dùng một `include/secrets.h` tạm, đã xóa sau khi build). Chưa thử trên mạch thật.

### 2.5 Dashboard chèn dữ liệu MQTT bằng `innerHTML`

**Hiện tượng và nguyên nhân.** Broker HiveMQ công cộng không xác thực, nên bất kỳ ai cũng publish được lên `nhom22/smartshelf/telemetry`. Dashboard chèn tên sản phẩm, nhóm hàng và mã kệ nhận được thẳng vào HTML: ở thẻ kệ, phần phân tích, nút điều khiển, danh sách chọn kệ, bảng lịch sử và thông báo (toast). Mã kệ còn nằm trong thuộc tính `onclick`. Vì vậy một tên sản phẩm chứa `<img src=x onerror=...>` sẽ chạy mã JavaScript trên trình duyệt người xem.

**Cách sửa** (`web/app.js`):
- Thêm hàm `escapeHtml()` và dùng cho mọi chuỗi lấy từ telemetry hoặc lịch sử trước khi chèn bằng `innerHTML`.
- Dashboard chỉ nhận mã kệ đúng dạng `K01`–`K10` (sau khi chuẩn hóa `K1` → `K01`) và bỏ qua mã lạ.
- Thông báo (toast) gán nội dung bằng `textContent` thay vì `innerHTML`.

**Kiểm chứng.** Mở `web/index.html` trong Chromium không giao diện (Playwright), chặn kết nối tới broker thật, rồi đưa vào telemetry giả có mã chèn ở tên sản phẩm, nhóm hàng và mã kệ.

| | Trước khi sửa | Sau khi sửa |
|---|---|---|
| Số lần mã chèn chạy | 100 | 0 |
| Số thẻ `<img>` bị chèn vào trang | 16 | 0 |
| Mã kệ lạ bị bỏ qua | Không | Có |
| Tên sản phẩm hiển thị | Hiển thị như chữ thường | Hiển thị như chữ thường |
| Lỗi JavaScript | 0 | 0 |

## 3. Tổng hợp kiểm thử

| Phép thử | Công cụ | Kết quả sau khi sửa |
|---|---|---|
| Biên dịch firmware, chế độ Wokwi | `pio run` | Thành công; RAM 15,8% (51.648 byte), Flash 80,2% |
| Biên dịch firmware, chế độ TCA9548A | `pio run` với `OLED_USE_TCA9548A 1` | Thành công |
| Kích thước telemetry so với giới hạn | `kich_thuoc_telemetry.py` | 1.042–1.108 byte, dưới mức 2.014 byte cho phép |
| Cú pháp mọi node function (24 + 24 + 25) | `kiem_tra_nodered.js` | 0 lỗi (trước khi sửa: 1 lỗi mỗi file) |
| Chuỗi Engine → Kiểm tra ngưỡng → Telegram | `kiem_tra_nodered.js` | Đạt cả 3 file |
| Chèn mã HTML trên dashboard | `kiem_tra_web.py` | Đạt (trước khi sửa: không đạt) |
| Cú pháp `web/app.js` | `node --check` | Đạt |

Các script nằm trong `kiem_thu/nhanh_moi/` của thư mục tiểu luận. Chạy lại bằng các lệnh:

```text
node   kiem_tra_nodered.js   E:\Ke_Hang_Thong_Minh_IOT
python kiem_tra_web.py       E:\Ke_Hang_Thong_Minh_IOT\web
python kich_thuoc_telemetry.py
```

## 4. Tệp đã thay đổi

| Tệp | Thay đổi |
|---|---|
| `src/main.cpp` | Bộ đệm MQTT/JSON, kiểm tra độ dài, log lỗi gửi; hai bộ TCA9548A |
| `include/secrets.example.h` | Chú thích nối hai bộ TCA9548A |
| `docker/nodered/data/flows.json` | Sửa lỗi cú pháp Engine; escape MarkdownV2 |
| `nodered_flow.json` | Như trên |
| `combined_flows.json` | Sửa lỗi cú pháp Engine; viết lại hàm định dạng Telegram |
| `web/app.js` | `escapeHtml()`, kiểm tra mã kệ, toast dùng `textContent` |
| `README.md`, `HE_THONG_IOT_VA_DANH_SACH_THIET_BI.md` | Hướng dẫn nối hai bộ TCA9548A |
| `BAO_CAO_DOI_CHIEU_TAI_LIEU_VA_DU_AN.md` | Báo cáo đối chiếu viết lại theo commit `0cebea0` |

## 5. Hạn chế và việc chưa làm

- **Chưa chạy trên môi trường thật:** chưa mô phỏng Wokwi, chưa chạy container Docker, chưa gửi tin Telegram thật, chưa lắp mạch. Phần Telegram mới kiểm tra đến mức nội dung tin đúng cú pháp MarkdownV2, chưa nhận phản hồi thật từ Telegram.
- **Giới hạn độ dài telemetry:** nếu đổi tên cả 10 sản phẩm thành tên rất dài bằng lệnh `set`, bản tin có thể vượt 1.536 byte. Khi đó firmware ghi log lỗi và bỏ qua lần gửi, chứ không gửi JSON hỏng.
- **Broker công cộng:** bản sửa ở mục 2.5 chỉ ngăn mã chạy trên dashboard. Người lạ vẫn gửi được telemetry giả và lệnh `set`/`tare` tới ESP32. Triển khai thật cần broker riêng có tài khoản/mật khẩu và TLS.
- **Các điểm nhỏ chưa sửa** (mục 8.6 của báo cáo đối chiếu): nhãn Kệ 2 và Kệ 3 trong Node-RED bị đảo so với firmware; lệnh `tare` qua MQTT chưa cập nhật LED ngay; tin Telegram trả lời từ firmware chưa escape tên sản phẩm; LED của K01, K05, K07 nằm trên chân khởi động GPIO2, GPIO12, GPIO15.

## 6. Việc cần làm tiếp

1. Xem lại thay đổi bằng `git diff`, rồi commit và push lên nhánh `fix/smartshelf-reliability` hoặc tạo pull request cho chủ repo.
2. Nạp lại flow vào Node-RED: khởi động lại container (`docker compose restart nodered`) hoặc import lại `docker/nodered/data/flows.json`.
3. Chạy mô phỏng Wokwi và xem Serial Monitor có dòng `[MQTT PUB] -> nhom22/smartshelf/telemetry (10 kệ: ... bytes)`.
4. Thu hồi token Telegram cũ bằng BotFather (token vẫn còn trong lịch sử Git), tạo `.env` và `include/secrets.h` với token mới.
