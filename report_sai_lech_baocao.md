# BÁO CÁO SAI LỆCH: Báo cáo PDF vs Code Thực Tế (v2)

> **Dự án:** Vi Điều Khiển Mở Rộng – Bài 3
> **Ngày:** 24/08/2026
> *(Đã loại bỏ các điểm liên quan Matlab/Python — user đã có code Matlab)*

---

## TỔNG QUAN

| Hạng mục | Số điểm sai lệch |
|---|---|
| Kiến trúc tác vụ FreeRTOS | **2 điểm** |
| Pin assignment / GPIO | **2 điểm** |
| Logic điều khiển động cơ | **3 điểm** |
| Lưu đồ Encoder | **1 điểm** |
| Lưu đồ giao tiếp ESP | **2 điểm** |
| **Tổng** | **10 điểm** |

---

## 1. KIẾN TRÚC FREERTOS

### Điểm 1 — Thiếu OTATask trong mô tả tác vụ

| | Nội dung |
|---|---|
| **Báo cáo ghi** | Mô tả 5 tác vụ: DefaultTask, InputTask, MotorTask, EncoderTask, ESPTask |
| **Code thực tế** | Có **6 tác vụ** — thêm **OTATask** (freertos.c dòng 202-203) |
| **OTATask hoạt động** | Giữ nút PB12 liên tục 3 giây → ghi cờ OTA vào Flash → reset STM32 → vào Bootloader cập nhật firmware qua WiFi |
| **Cần thêm vào báo cáo** | Bổ sung mô tả OTATask trong Mục 1.2 (Phân tích chức năng) và danh sách task |

### Điểm 2 — Lưu đồ 4.1 thiếu OTATask chạy song song

| | Nội dung |
|---|---|
| **Báo cáo ghi** | Lưu đồ chính vẽ 3 bước xử lý tuần tự: Motor → Timer → ESP |
| **Code thực tế** | OTATask chạy **song song hoàn toàn độc lập** (daemon thread), không nằm trong vòng lặp DefaultTask |
| **Cần sửa** | Thêm ghi chú bên cạnh lưu đồ: *"OTATask: chạy nền song song — kích hoạt bằng PB12 giữ 3 giây"* |

---

## 2. GPIO / PIN ASSIGNMENT

### Điểm 3 — Nút STOP sai chân

| | Nội dung |
|---|---|
| **Báo cáo ghi** | Chương 3: *"nút dừng (BTN STOP) nối vào chân PB10"* |
| **Code thực tế** | `cur_but3 = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11)` (freertos.c dòng 251) |
| **Chân đúng** | **PB11** |
| **Cần sửa** | Sửa PB10 → **PB11** trong Chương 3 và sơ đồ kết nối |

### Điểm 4 — Chân TB6612 Driver sai

| | Nội dung |
|---|---|
| **Báo cáo ghi** | Chương 3: *"AI1 nối PB4, AI2 nối PB7, STBY nối PB8"* |
| **Code FORWARD** | `PB7=SET, PB4=SET, PB6=RESET` (freertos.c dòng 325-327) |
| **Code REVERSE** | `PB7=SET, PB4=RESET, PB6=SET` (freertos.c dòng 352-354) |
| **Code STOP** | `PB7=RESET, PB4=RESET, PB6=RESET` (freertos.c dòng 361-363) |
| **Kết luận thực tế** | AI1 = PB4, AI2 = **PB6**, STBY = **PB7** |
| **Cần sửa** | Đổi thành: *"AI1 nối PB4, AI2 nối **PB6**, STBY nối **PB7**"* |

---

## 3. LOGIC ĐIỀU KHIỂN ĐỘNG CƠ

### Điểm 5 — Lưu đồ 4.2 thiếu bước "chờ motor dừng hẳn" trước đổi chiều

| | Nội dung |
|---|---|
| **Báo cáo ghi** | Lệnh THUẬN/NGHỊCH → cập nhật trạng thái IN1/IN2 ngay lập tức |
| **Code thực tế** | Trước khi đổi chiều: **set PWM = 0**, sau đó **chờ RPM < 10** (hoặc timeout 1 giây = 100 lần x 10ms) mới cập nhật IN1/IN2 (freertos.c dòng 308-323) |
| **Mục đích** | Bảo vệ motor và driver không bị đảo chiều đột ngột |
| **Cần thêm vào lưu đồ 4.2** | Thêm 2 bước trước "Cập nhật trạng thái": (1) *"Set PWM = 0"* → (2) *"Chờ RPM < 10 hoặc timeout 1s"* |

### Điểm 6 — LED báo hiệu sai màu/chân so với code

| | Nội dung |
|---|---|
| **Báo cáo ghi** | LED Xanh sáng khi chạy thuận, LED Vàng = cảnh báo nghịch, LED Đỏ = dừng |
| **Code FORWARD** | Bật `PA4` = LED Vàng (dòng 329) |
| **Code REVERSE** | Bật `PA5` = LED Đỏ (dòng 356) |
| **Code STOP** | Bật `PA7` (dòng 365) — PA7 là chân nào cần xác nhận theo sơ đồ |
| **LED Xanh (PA3)** | Không bật trực tiếp trong MotorTask — chỉ sáng khi không có cờ Timer nào |
| **Cần kiểm tra** | Xác nhận lại mapping: PA3=Xanh, PA4=Vàng, PA5=Đỏ, PA7=? rồi sửa mô tả |

### Điểm 7 — Lưu đồ 4.2 vẽ sai cơ chế nhận lệnh

| | Nội dung |
|---|---|
| **Báo cáo ghi** | Lưu đồ vẽ: kiểm tra trực tiếp "Lệnh THUẬN?" → rồi kiểm tra "cờ Vàng == 0 và cờ Đỏ == 0?" |
| **Code thực tế** | Dùng **osMessageQueue**: InputTask đưa lệnh vào queue → MotorTask đọc queue bằng `osMessageQueueGet()` (dòng 303) → xử lý switch-case |
| **Sự khác biệt** | Không có kiểm tra cờ trước khi nhận lệnh từ queue — lệnh được nhận vô điều kiện từ queue, chỉ logic bên trong case mới phân biệt |
| **Cần sửa** | Cập nhật lưu đồ 4.2 thể hiện: *"Có lệnh trong Queue?"* thay cho kiểm tra trực tiếp |

---

## 4. LƯU ĐỒ ENCODER

### Điểm 8 — Lưu đồ 4.4 gọi là "ngắt" nhưng code dùng Hardware Timer Mode + Task

| | Nội dung |
|---|---|
| **Báo cáo ghi** | Tiêu đề: *"Lưu đồ ngắt đọc Encoder"* — vẽ theo dạng ISR (interrupt service routine) |
| **Code thực tế** | Dùng **Timer2 Hardware Encoder Mode** — STM32 tự đếm xung A/B bằng phần cứng. Code chỉ đọc counter: `__HAL_TIM_GET_COUNTER(&htim2)` trong **EncoderTask** (polling mỗi 10ms) (freertos.c dòng 392-408) |
| **Không có ISR** | Không có hàm `HAL_GPIO_EXTI_Callback` hay tương tự cho encoder trong code |
| **Cần sửa** | Đổi tiêu đề → *"Lưu đồ tác vụ đọc Encoder (Timer2 Hardware Encoder Mode)"* và sửa mô tả từ "ngắt kích hoạt" → "EncoderTask đọc timer mỗi 10ms" |

---

## 5. LƯU ĐỒ GIAO TIẾP ESP (4.5)

### Điểm 9 — Thiếu logic Reconnect trong lưu đồ

| | Nội dung |
|---|---|
| **Báo cáo ghi** | Lưu đồ 4.5 chỉ vẽ: kiểm tra lệnh → gửi data → trở về |
| **Code thực tế** | Đầu vòng lặp for(;;): kiểm tra `connection_lost` → nếu mất kết nối: gửi `AT+CIPCLOSE` → gọi lại `ESP_Init("172.20.10.7", 8000)` → thử lại (freertos.c dòng 453-466) |
| **Cần thêm vào lưu đồ** | Thêm điều kiện đầu vòng lặp: *"Kết nối mất?"* → Yes: *"AT+CIPCLOSE → ESP_Init() → Reconnect"* |

### Điểm 10 — Thiếu cơ chế đếm lỗi fail_count

| | Nội dung |
|---|---|
| **Báo cáo ghi** | Không đề cập xử lý lỗi gửi |
| **Code thực tế** | Sau `ESP_SendData()`: nếu result==1 → OK; result==2 → mất kết nối ngay; result khác → tăng `fail_count`, nếu >= 5 → set `connection_lost=1` trigger reconnect (freertos.c dòng 498-516) |
| **Cần thêm vào lưu đồ** | Sau "Gửi data": *"Gửi thành công?"* → No: *"fail_count++"* → *"fail_count >= 5?"* → Yes: *"Trigger Reconnect"* |

---

## BẢNG TÓM TẮT HÀNH ĐỘNG

| # | Vị trí cần sửa | Nội dung sửa | Mức độ |
|---|---|---|---|
| 1 | Mục 1.2 + danh sách task | Bổ sung mô tả **OTATask** (PB12, giữ 3s → OTA update) | QUAN TRỌNG |
| 2 | Lưu đồ 4.1 | Thêm ghi chú OTATask chạy nền song song | QUAN TRỌNG |
| 3 | Chương 3 — kết nối | Sửa BTN STOP: **PB10 → PB11** | BẮT BUỘC |
| 4 | Chương 3 — kết nối | Sửa TB6612: AI2 **PB7→PB6**, STBY **PB8→PB7** | BẮT BUỘC |
| 5 | Lưu đồ 4.2 | Thêm 2 bước: Set PWM=0 → Chờ RPM<10 trước đổi chiều | QUAN TRỌNG |
| 6 | Mô tả LED (1.2 + 4.2) | Xác nhận lại màu LED theo PA3/PA4/PA5 | QUAN TRỌNG |
| 7 | Lưu đồ 4.2 | Sửa cơ chế nhận lệnh → thể hiện Queue | NÊN SỬA |
| 8 | Lưu đồ 4.4 + tiêu đề | Đổi "ngắt" → "Timer2 Hardware Encoder Mode, đọc mỗi 10ms" | QUAN TRỌNG |
| 9 | Lưu đồ 4.5 | Thêm nhánh Reconnect TCP đầu vòng lặp | QUAN TRỌNG |
| 10 | Lưu đồ 4.5 | Thêm logic fail_count >= 5 → trigger reconnect | NÊN SỬA |

**Mức độ:**
- **BẮT BUỘC** — Sai thông tin kỹ thuật, ảnh hưởng điểm đánh giá
- **QUAN TRỌNG** — Thiếu nội dung quan trọng, nên bổ sung
- **NÊN SỬA** — Chi tiết chưa đầy đủ, cải thiện chất lượng báo cáo
