# 📋 KẾ HOẠCH NÂNG CẤP HỆ THỐNG SMART SHELF IOT
## MÔ HÌNH 10 KỆ HÀNG TẠP HÓA THÔNG MINH (POC 50 SẢN PHẨM)

> **Dự án:** Quản trị & Giám sát Kệ hàng Thông minh cho Tiệm Tạp hóa (Smart Grocery Store 4.0)  
> **Nhóm thực hiện:** Nhóm 22  
> **Phiên bản:** v2.0 (Mở rộng từ 3 kệ lên 10 kệ phân tán)  
> **Trạng thái:** 🚀 Đang triển khai (In Progress)  

---

## 🎯 1. BỐI CẢNH VÀ MỤC TIÊU NÂNG CẤP

* **Bối cảnh thực tế:** Một tiệm tạp hóa nhỏ kinh doanh khoảng 50 mặt hàng. Thực tế không thể gắn 50 bo mạch Wi-Fi riêng lẻ vì gây nghẽn router và tốn kém nguồn điện.
* **Giải pháp kiến trúc:**
  * Toàn bộ cửa hàng được tổ chức thành **10 phân khu / cụm kệ (Cluster)**.
  * Mỗi cụm đại diện cho 1 nhóm sản phẩm (Nước giải khát, Mì gói, Sữa, Dầu ăn, Nước mắm, Bột giặt, Gia vị...).
  * **Mô hình PoC (Proof of Concept) 10 Kệ**: Triển khai và mô phỏng 10 kệ hàng hoàn chỉnh với màn hình hiển thị riêng (ESL) và đèn cảnh báo tại chỗ. Khi đưa vào thực tế 50 sản phẩm, chỉ cần nhân rộng các cụm mà không cần viết lại kiến trúc hệ thống.
* **Mô hình phối hợp mô phỏng (Hybrid Simulation Architecture):**
  * **Node 1 (Wokwi Interactive):** ESP32 thật mô phỏng trên Wokwi quản lý Phân khu A (Kệ 1, Kệ 2, Kệ 3) có cảm biến HX711, đèn LED cảnh báo và màn hình OLED SSD1306 trực quan để tương tác trực tiếp khi báo cáo.
  * **Virtual Shelves (Node-RED Engine):** Quản lý và tự động sinh biến thiên dữ liệu cho 7 kệ còn lại (Kệ 4 $\rightarrow$ Kệ 10) mô phỏng khách hàng vào mua sắm trong tiệm.
  * **Hạ tầng Tầng 2 & 3:** MQTT Broker HiveMQ, Node-RED và Web Dashboard xử lý đồng bộ mảng động 10 kệ hàng.

---

## 📦 2. DANH MỤC 10 SẢN PHẨM TẠP HÓA CHUẨN HÓA

| Mã Kệ | Tên Sản Phẩm | Phân Loại | Khối Lượng 1 Đơn Vị | Ngưỡng Cảnh Báo (Min) | Môi Trường Mô Phỏng |
| :---: | :--- | :--- | :---: | :---: | :---: |
| **K01** | **Coca-Cola 330ml** | Đồ uống giải khát | 330g / lon | $\le$ 3 lon | Wokwi (Cảm biến 1) |
| **K02** | **Mì Hảo Hảo Tôm Chua Cay** | Thực phẩm ăn liền | 75g / gói | $\le$ 5 gói | Wokwi (Cảm biến 2) |
| **K03** | **Bánh Oishi Phồng Tôm** | Bánh kẹo & Snack | 50g / gói | $\le$ 4 gói | Wokwi (Cảm biến 3) |
| **K04** | **Sữa tươi Vinamilk 180ml** | Sữa & Sản phẩm từ sữa | 190g / hộp | $\le$ 4 hộp | Node-RED Generator |
| **K05** | **Bột giặt OMO Comfort 800g** | Hóa mỹ phẩm & Giặt xả | 800g / túi | $\le$ 2 túi | Node-RED Generator |
| **K06** | **Dầu ăn Simply 1L** | Dầu ăn & Gia vị nấu | 920g / chai | $\le$ 3 chai | Node-RED Generator |
| **K07** | **Nước mắm Nam Ngư 500ml** | Gia vị & Nước chấm | 550g / chai | $\le$ 3 chai | Node-RED Generator |
| **K08** | **Bánh Chocopie Hộp 6 cái** | Bánh kẹo đóng hộp | 198g / hộp | $\le$ 2 hộp | Node-RED Generator |
| **K09** | **Trứng gà Ba Huân 10 quả** | Thực phẩm tươi sống | 600g / vỉ | $\le$ 2 vỉ | Node-RED Generator |
| **K10** | **Tương ớt Chinsu 250g** | Gia vị & Đồ chấm | 280g / chai | $\le$ 3 chai | Node-RED Generator |

---

## 📝 3. DANH SÁCH CÔNG VIỆC CẦN THỰC HIỆN (CHECKLIST)

### Giai đoạn 1: Chuẩn hóa Sơ đồ Mạch Wokwi & Firmware ESP32
- [x] **1.1. Cập nhật `diagram.json`:**
  - Định danh lại các thành phần phần cứng tương ứng Phân khu A (K01: Coca, K02: HaoHao, K03: Oishi) trong hệ thống 10 kệ tạp hóa.
  - Chuẩn hóa kết nối: OLED (I2C 21/22), Loadcell 1 (19/18), Loadcell 2 (5/17), Loadcell 3 (25/26), LED đỏ (2, 4, 27).
- [x] **1.2. Nâng cấp Firmware `src/main.cpp`:**
  - Cập nhật định danh sản phẩm K01, K02, K03 chuẩn theo danh mục tạp hóa (Coca 330g, Hảo Hảo 75g, Oishi 50g).
  - Chuẩn hóa payload JSON telemetry: bổ sung `node_id: "NODE_01"`, `store: "TapHoaSmart"`, `zone: "Khu A - Đồ uống & Ăn liền"`.
  - Nâng cấp giao diện OLED SSD1306: Hiển thị thương hiệu "TAP HOA 4.0", hiển thị luân phiên tình trạng 3 kệ và badge cảnh báo.
  - Hỗ trợ giao thức lệnh điều khiển `nhom22/smartshelf/cmd` (Tare, Set thông số linh hoạt cho cả `K1` và `K01`).

### Giai đoạn 2: Nâng cấp Flow Node-RED (Tầng 2 Server)
- [x] **2.1. Cập nhật `nodered_flow.json`:**
  - **Bộ thu nhận MQTT:** Lắng nghe topic `nhom22/smartshelf/telemetry`.
  - **10-Shelf Grocery Engine (`fn_grocery_10_engine`):** Tích hợp bộ sinh dữ liệu thông minh cho K04 $\rightarrow$ K10 với kịch bản khách mua hàng/nhập hàng thực tế.
  - **Tổng hợp số liệu & Lưu trữ Context:** Tính toán Tổng sản phẩm tồn (món), Tổng khối lượng (g), Số kệ thiếu hàng trên cả 10 kệ.
  - **Cảnh báo Telegram Bot:** Kích hoạt cảnh báo tự động khi bất kỳ kệ nào trong 10 kệ có `low_stock = true`.
  - **Điều khiển Downlink:** Hỗ trợ định tuyến lệnh Tare và Set cấu hình cho toàn bộ 10 kệ.
- [x] **2.2. Đồng bộ hóa sang `combined_flows.json`**.

### Giai đoạn 3: Nâng cấp Giao diện Web Dashboard (Tầng 3 App)
- [x] **3.1. Cập nhật `web/index.html`:**
  - Chuyển đổi giao diện từ 3 thẻ cố định sang **Lưới 10 Kệ hàng Tạp hóa (10-Shelf Grid)** sinh động, chuyên nghiệp.
  - Thêm thanh lọc danh mục: Tất cả (10), Đang thiếu hàng (Alert), Đồ uống, Ăn liền, Gia vị, Bánh kẹo, Sữa tươi, Hóa phẩm, Thực phẩm.
  - Cập nhật 4 thẻ KPI tổng quan: Tổng 10 mặt hàng, Tải trọng kho, Kệ cần nhập, Trạng thái hệ thống Tạp Hóa 4.0.
  - Modal cấu hình & Tare mở rộng: Cho phép chọn và cấu hình bất kỳ kệ nào từ K01 đến K10.
- [x] **3.2. Cập nhật `web/app.js`:**
  - Mở rộng đối tượng trạng thái `shelfState` quản lý đủ 10 sản phẩm K01 $\rightarrow$ K10.
  - Nâng cấp Chart.js: Vẽ biểu đồ thời gian thực cho 10 kệ hàng với bảng 10 màu sắc phân biệt rõ ràng.
  - Tích hợp nút tương tác nhanh `-1 Mua` và `+1 Nhập` trực tiếp trên từng thẻ kệ để phục vụ thuyết trình trực quan.
  - Nâng cấp chế độ Giả lập Demo: Tự động chạy biến động dữ liệu cho cả 10 kệ khi chưa bật Wokwi/ESP32.
  - Hỗ trợ xuất file Excel/CSV báo cáo kiểm kê 10 mặt hàng.
- [x] **3.3. Tinh chỉnh CSS (`web/style.css`):**
  - Tối ưu giao diện Responsive dạng thẻ Grid cho 10 kệ (2 cột trên tablet, 3-4 cột trên desktop), hiệu ứng Glassmorphism.

### Giai đoạn 4: Kiểm thử tích hợp & Hoàn thiện Tài liệu
- [x] **4.1. Kiểm thử luồng dữ liệu:** Wokwi $\rightarrow$ MQTT HiveMQ $\rightarrow$ Node-RED $\rightarrow$ Web Dashboard.
- [x] **4.2. Cập nhật tài liệu kiến trúc:** `HE_THONG_IOT_VA_DANH_SACH_THIET_BI.md` (bổ sung Phần F).
- [x] **4.3. Cập nhật Nhật ký hoàn thành vào bản kế hoạch này.**

---

## 📌 4. NHẬT KÝ THỰC HIỆN (EXECUTION LOG)

| Thời Gian | Hạng Mục | Nội Dung Thực Hiện | Kết Quả |
| :--- | :--- | :--- | :---: |
| *Khởi tạo* | Lập kế hoạch | Tạo tài liệu kế hoạch tổng thể nâng cấp 10 kệ tạp hóa | ✅ Đã duyệt |
| *Bước 1* | Wokwi & Diagram | Nâng cấp sơ đồ `diagram.json` với **10 MÀN HÌNH OLED RIÊNG BIỆT (ESL)** + 10 Loadcell + 10 LED + Đi dây Bus Daisy-Chain song song siêu gọn gàng (không chéo dây, không rối) | ✅ Hoàn thành xuất sắc |
| *Bước 2* | Firmware ESP32 | Nâng cấp `src/main.cpp` điều khiển độc lập **10 màn hình OLED riêng biệt** (địa chỉ `0x30`..`0x39` trên I2C Bus), hiển thị tên hàng, số lượng lớn, tải trọng và trạng thái | ✅ Hoàn thành xuất sắc |
| *Bước 3* | Node-RED Flow | Tích hợp `🛒 10-Shelf Grocery Engine`, bảng HTML 10 kệ, cảnh báo Telegram 10 kệ trong `nodered_flow.json` & `combined_flows.json` | ✅ Hoàn thành |
| *Bước 4* | Web Dashboard | Mở rộng giao diện Grid 10 kệ, Chart.js 10 đường, bộ lọc danh mục, nút thử nghiệm Mua/Nhập nhanh trong `web/` | ✅ Hoàn thành |
| *Bước 5* | Tài liệu & Kiểm thử | Cập nhật tài liệu Phần F trong `HE_THONG_IOT_VA_DANH_SACH_THIET_BI.md` và nghiệm thu | ✅ Hoàn thành |
| *Bước 6* | Node-RED: Khởi động & Deploy Flow | Khởi động lại server Node-RED (task-320), nạp `nodered_flow.json` vào `~/.node-red/flows.json`, deploy qua REST API thành công, MQTT HiveMQ reconnected | ✅ Hoàn thành |
| *Bước 7* | Telegram: Nâng cấp định dạng tin nhắn | Chuyển từ Markdown cũ → **MarkdownV2 chuẩn Telegram** với cấu trúc mới: Header đẹp, phân cấp Hết hàng 🔴 / Sắp hết 🟡, thanh tiến trình `████░`, Uptime ESP32, Footer mô tả hệ thống. Hỗ trợ cả 2 Chat ID, Rate limit 1 msg/s | ✅ Hoàn thành |
| *Bước 8* | Docker: Container hóa toàn bộ hệ thống | Khởi động Docker Desktop, thiết lập `docker-compose.yml` gồm 3 container độc lập (`smartshelf_mosquitto` port 1883/9001, `smartshelf_nodered` port 1880, `smartshelf_dashboard` port 8088), mount volume dữ liệu luồng 10 kệ và kiểm thử kết nối thành công | ✅ Hoàn thành xuất sắc |

