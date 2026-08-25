# 🎛️ Hệ Thống Điều Khiển & Giám Sát Động Cơ DC — STM32F103C8T6

> **Đồ án Vi Điều Khiển** — Điều khiển động cơ DC qua nút nhấn, đo tốc độ bằng Encoder, truyền dữ liệu realtime qua WiFi (ESP-01) và hỗ trợ cập nhật firmware từ xa (OTA).

---

## 📑 Mục Lục

- [Tổng Quan Dự Án](#-tổng-quan-dự-án)
- [Kiến Trúc Hệ Thống](#-kiến-trúc-hệ-thống)
- [Phần Cứng & Sơ Đồ Chân](#-phần-cứng--sơ-đồ-chân)
- [Chi Tiết Chức Năng](#-chi-tiết-chức-năng)
- [Cấu Trúc Phần Mềm Nhúng (FreeRTOS)](#-cấu-trúc-phần-mềm-nhúng-freertos)
- [Giao Thức Truyền Dữ Liệu](#-giao-thức-truyền-dữ-liệu)
- [OTA Bootloader](#-ota-bootloader)
- [Phần Mềm PC](#-phần-mềm-pc)
- [Cấu Trúc Thư Mục](#-cấu-trúc-thư-mục)
- [Hướng Dẫn Sử Dụng](#-hướng-dẫn-sử-dụng)
- [Yêu Cầu Phần Mềm](#-yêu-cầu-phần-mềm)

---

## 🔭 Tổng Quan Dự Án

| Thông số | Chi tiết |
|---|---|
| **Vi điều khiển** | STM32F103C8T6 (Blue Pill) — ARM Cortex-M3, 64KB Flash, 20KB SRAM |
| **RTOS** | FreeRTOS (CMSIS-RTOS v2) |
| **Module WiFi** | ESP-01 (ESP8266) — Giao tiếp AT Command qua UART |
| **IDE** | Keil MDK-ARM v5 + STM32CubeMX |
| **Clock** | HSI 8 MHz (Internal RC) |
| **Toolchain PC** | Python 3.x (Tkinter + Matplotlib) |

### Chức năng chính:
1. **Điều khiển động cơ DC** — Quay thuận / Quay nghịch / Dừng thông qua 3 nút nhấn
2. **Điều chỉnh tốc độ** — Biến trở (Potentiometer) → ADC → PWM duty cycle
3. **Đo Encoder** — Đọc vị trí (°) và tốc độ (RPM) từ Rotary Encoder
4. **Truyền dữ liệu WiFi** — Gửi JSON qua TCP/IP đến phần mềm giám sát trên PC (~10 Hz)
5. **Giám sát Realtime** — GUI Python hiển thị biểu đồ ADC, PWM, Position, RPM
6. **Cập nhật firmware OTA** — Tải firmware mới từ HTTP Server qua ESP-01, ghi vào Flash

---

## 🏗️ Kiến Trúc Hệ Thống

```
┌─────────────────────────────────────────────────────────────┐
│                        STM32F103C8T6                        │
│                                                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────┐  │
│  │InputTask │  │MotorTask │  │Encoder   │  │  ESPTask   │  │
│  │(Nút+ADC) │→ │(PWM+DIR) │  │  Task    │  │ (TCP/JSON) │  │
│  └──────────┘  └──────────┘  └──────────┘  └─────┬──────┘  │
│       ↓ Queue       ↑ Mutex      ↑ Mutex         │ UART    │
│  ┌──────────┐       │            │               │         │
│  │ OTATask  │       └────────────┘               │         │
│  │(Nút OTA) │                                    │         │
│  └──────────┘                                    │         │
└──────────────────────────────────────────────────┼─────────┘
                                                   │
                                            ┌──────┴──────┐
                                            │   ESP-01    │
                                            │  (WiFi)     │
                                            └──────┬──────┘
                                                   │ TCP/IP
                                            ┌──────┴──────┐
                                            │   PC/Laptop │
                                            │ Motor       │
                                            │ Monitor GUI │
                                            └─────────────┘
```

---

## 🔌 Phần Cứng & Sơ Đồ Chân

### Bảng Phân Bố Chân STM32F103C8T6

| Chân STM32 | Chức năng | Mô tả | Ghi chú |
|:---:|:---|:---|:---|
| **PA0** | TIM2_CH1 (Encoder A) | Kênh A của Rotary Encoder | Input, No Pull |
| **PA1** | TIM2_CH2 (Encoder B) | Kênh B của Rotary Encoder | Input, No Pull |
| **PA2** | ADC1_IN2 (Analog) | Đọc biến trở điều chỉnh tốc độ | Analog Input |
| **PA4** | GPIO Output | 🟢 LED Xanh — Báo chế độ FORWARD | Push-Pull |
| **PA5** | GPIO Output | 🟡 LED Vàng — Báo chế độ REVERSE | Push-Pull |
| **PA7** | GPIO Output | 🔴 LED Đỏ — Báo chế độ STOP | Push-Pull |
| **PA8** | TIM1_CH1 (PWM) | Tín hiệu PWM điều khiển tốc độ motor | AF Push-Pull |
| **PA9** | USART1_TX | Truyền dữ liệu đến ESP-01 (TX) | AF Push-Pull |
| **PA10** | USART1_RX | Nhận dữ liệu từ ESP-01 (RX) | Input Floating |
| **PA13** | SWDIO | Nạp/Debug SWD | Hệ thống |
| **PA14** | SWCLK | Nạp/Debug SWD | Hệ thống |
| **PB0** | GPIO Input | 🔘 Nút nhấn 1 — **FORWARD** (Quay thuận) | Pull-Up, nhấn = LOW |
| **PB4** | GPIO Output | Điều khiển hướng motor (DIR 1) | Push-Pull |
| **PB6** | GPIO Output | Điều khiển hướng motor (DIR 2) | Push-Pull |
| **PB7** | GPIO Output | Điều khiển Enable motor driver | Push-Pull |
| **PB10** | GPIO Input | 🔘 Nút nhấn 2 — **REVERSE** (Quay nghịch) | Pull-Up, nhấn = LOW |
| **PB11** | GPIO Input | 🔘 Nút nhấn 3 — **STOP** (Dừng motor) | Pull-Up, nhấn = LOW |
| **PB12** | GPIO Input | 🔘 Nút nhấn 4 — **OTA** (Giữ 3s để cập nhật) | Pull-Down, nhấn = HIGH |

### Sơ Đồ Kết Nối ESP-01

```
 STM32F103         ESP-01
 ─────────         ──────
   PA9 (TX)  ───→  RX
   PA10 (RX) ←───  TX
   3.3V      ───→  VCC, CH_PD
   GND       ───→  GND
```

> ⚠️ **Lưu ý**: ESP-01 chạy ở 3.3V. Không nối 5V vào ESP-01!

### Sơ Đồ Kết Nối Motor Driver

```
 STM32F103         Motor Driver (L298N / TB6612 / tương đương)
 ─────────         ────────────
   PA8 (PWM) ───→  ENA (Speed Control)
   PB7 (EN)  ───→  Enable
   PB4 (DIR1)───→  IN1
   PB6 (DIR2)───→  IN2
```

| Chế độ | PB7 (EN) | PB4 (IN1) | PB6 (IN2) | PA8 (PWM) |
|:---:|:---:|:---:|:---:|:---:|
| **FORWARD** | HIGH | HIGH | LOW | Duty từ ADC |
| **REVERSE** | HIGH | LOW | HIGH | Duty từ ADC |
| **STOP** | LOW | LOW | LOW | 0% |

### Sơ Đồ Kết Nối Encoder

```
 Rotary Encoder     STM32F103
 ──────────────     ─────────
   Kênh A     ───→  PA0 (TIM2_CH1)
   Kênh B     ───→  PA1 (TIM2_CH2)
   VCC        ───→  3.3V / 5V
   GND        ───→  GND
```

- **Encoder Mode**: TI1 và TI2 (đếm cả 2 kênh = x4 resolution)
- **CPR**: 1562 xung/vòng (cấu hình trong code, thay đổi theo loại encoder)
- **Timer Period**: 65535 (16-bit counter)

### Sơ Đồ Kết Nối Nút Nhấn

```
 Nút FORWARD (PB0)     Nút REVERSE (PB10)    Nút STOP (PB11)
 ──────────────────     ──────────────────     ─────────────────
       PB0                    PB10                  PB11
        │                      │                     │
        ┤                      ┤                     ┤
       ─┴─ GND               ─┴─ GND              ─┴─ GND
   (Pull-Up nội)          (Pull-Up nội)         (Pull-Up nội)
   Nhấn = LOW             Nhấn = LOW            Nhấn = LOW

 Nút OTA (PB12)
 ───────────────
       PB12
        │
        ┤
       ─┴─ VCC
   (Pull-Down nội)
   Nhấn = HIGH
   Giữ 3 giây để kích hoạt OTA
```

### Sơ Đồ Kết Nối LED Chỉ Thị

```
   PA4 ──→ 🟢 LED Xanh  (FORWARD)
   PA5 ──→ 🟡 LED Vàng  (REVERSE)
   PA7 ──→ 🔴 LED Đỏ    (STOP)
   
   (Mỗi LED nối nối tiếp điện trở 330Ω xuống GND)
```

---

## ⚙️ Chi Tiết Chức Năng

### 1. Điều Khiển Hướng Quay Motor

- Nhấn **nút PB0** → Motor quay **THUẬN** (FORWARD), LED xanh sáng
- Nhấn **nút PB10** → Motor quay **NGHỊCH** (REVERSE), LED vàng sáng
- Nhấn **nút PB11** → Motor **DỪNG** (STOP), LED đỏ sáng
- Khi chuyển hướng, hệ thống **chờ motor dừng hẳn** (RPM < 10) trước khi đổi chiều (bảo vệ motor)
- Timeout chờ dừng: tối đa **1 giây** (100 × 10ms)

### 2. Điều Chỉnh Tốc Độ

- Biến trở nối vào **PA2** (ADC1 Channel 2)
- Giá trị ADC: **0 – 4095** (12-bit)
- Chuyển đổi: `Duty (%) = ADC × 100 / 4095`
- PWM output trên **PA8** (TIM1 Channel 1)
- Tần số PWM: `8 MHz / (3599 + 1) = 2.222 kHz`

### 3. Đo Encoder

- Sử dụng **TIM2 Encoder Mode** (đếm cả TI1 và TI2)
- Tính toán mỗi **10ms**:
  - **Vị trí (°)** = `(counter × 360) / CPR`
  - **Tốc độ (RPM)** = `(Δcounter × 6000) / CPR`
  - CPR mặc định: **1562** xung/vòng

### 4. Truyền Dữ Liệu WiFi

- ESP-01 kết nối WiFi → TCP Client kết nối đến PC (TCP Server)
- Gửi dữ liệu JSON mỗi **80ms** (~12.5 Hz)
- Có CRC8 checksum để kiểm tra tính toàn vẹn dữ liệu
- Tự động reconnect khi mất kết nối (sau 5 lần gửi thất bại)

### 5. OTA (Over-The-Air Update)

- Nhấn giữ **nút PB12** trong **3 giây** → LED nhấp nháy xác nhận → MCU reset vào Bootloader
- Bootloader tải firmware mới từ HTTP Server qua ESP-01
- Ghi firmware vào Flash từ địa chỉ `0x08002800`
- Sau khi hoàn tất → MCU reset và chạy firmware mới

---

## 🧵 Cấu Trúc Phần Mềm Nhúng (FreeRTOS)

### Danh Sách Task

| Task | Stack | Priority | Chu kỳ | Chức năng |
|:---|:---:|:---:|:---:|:---|
| `defaultTask` | 512B | Normal | 1ms | Task mặc định (idle) |
| `InputTask` | 1024B | Low | 10ms | Đọc nút nhấn + ADC, gửi lệnh vào Queue |
| `MotorTask` | 512B | Low | 10ms | Nhận lệnh từ Queue, điều khiển PWM + hướng motor |
| `EncoderTask` | 512B | Low | 10ms | Đọc Encoder, tính vị trí và RPM |
| `ESPTask` | 1024B | Low | 80ms | Kết nối WiFi, gửi JSON qua TCP |
| `OTATask` | 1024B | Low | 100ms | Phát hiện nút OTA, kích hoạt cập nhật |

### Cơ Chế Đồng Bộ

| Đối tượng | Loại | Mô tả |
|:---|:---|:---|
| `motorCommandQueue` | Message Queue (16 phần tử) | Truyền lệnh FORWARD/REVERSE/STOP từ InputTask → MotorTask |
| `systemStateMutex` | Mutex | Bảo vệ struct `infor` (dữ liệu chia sẻ giữa các task) |
| `myBinarySem01` | Binary Semaphore | Dự phòng |
| `myEvent01` | Event Flags | Dự phòng |

### Struct Dữ Liệu Chia Sẻ

```c
typedef enum mode_motor {
    FORWARD,    // 0
    REVERSE,    // 1
    STOP        // 2
} mode_motor;

typedef struct infor {
    mode_motor mode;       // Chế độ hiện tại
    uint32_t   adc_value;  // Giá trị ADC (0-4095)
    float      pwm;        // Duty cycle (0-100%)
    float      positionDeg;// Vị trí encoder (độ)
    float      rpm;        // Tốc độ motor (RPM)
} infor;
```

---

## 📡 Giao Thức Truyền Dữ Liệu

### Kết Nối Mạng

```
STM32 ←UART→ ESP-01 ←WiFi→ Router ←LAN→ PC (TCP Server port 8000)
```

- **WiFi SSID**: `tuikycuc` (cấu hình trong `usart.c` và `bootloader_main.c`)
- **TCP Server IP**: `172.20.10.7` (cấu hình trong `freertos.c`)
- **TCP Port (Data)**: `8000`
- **TCP Port (OTA)**: `8080`
- **UART Baudrate**: `115200`

### Định Dạng JSON

Mỗi gói dữ liệu được gửi dưới dạng JSON, kết thúc bằng `\n`:

```json
{"m":0,"a":2048,"d":50.0,"p":-45.50,"r":120.75,"s":123,"c":"A3"}
```

| Trường | Kiểu | Mô tả | Ví dụ |
|:---:|:---:|:---|:---:|
| `m` | int | Mode: 0=FORWARD, 1=REVERSE, 2=STOP | `0` |
| `a` | uint | Giá trị ADC (0-4095) | `2048` |
| `d` | float | PWM Duty Cycle (%) | `50.0` |
| `p` | float | Vị trí Encoder (°) | `-45.50` |
| `r` | float | Tốc độ motor (RPM) | `120.75` |
| `s` | uint8 | Sequence number (0-255, tự động wrap) | `123` |
| `c` | string | CRC8 Checksum (hex, 2 ký tự) | `"A3"` |

### CRC8 Checksum

- **Polynomial**: 0x07 (CRC8-CCITT)
- **Tính trên**: Chuỗi JSON payload gốc (trước khi thêm trường `"c"`)
- **Ví dụ**: Tính CRC8 trên `{"m":0,"a":2048,"d":50.0,"p":-45.50,"r":120.75,"s":123}` → `"A3"`

---

## 🔄 OTA Bootloader

### Bố Cục Bộ Nhớ Flash (64KB)

```
┌────────────────────────────────────────────┐
│ 0x08000000 ┃ Bootloader (10KB)             │ ← Chạy đầu tiên
│            ┃ 0x08000000 - 0x080027FF       │
├────────────────────────────────────────────┤
│ 0x08002800 ┃ Application (54KB)            │ ← Firmware chính
│            ┃ 0x08002800 - 0x0800FFFF       │
└────────────────────────────────────────────┘
```

### Quy Trình OTA

```
1. Người dùng nhấn giữ nút PB12 (3 giây)
2. LED nhấp nháy 10 lần xác nhận
3. Ghi cờ OTA (0xDEADBEEF) vào SRAM 0x20004000
4. MCU Reset → Bootloader đọc cờ
5. Bootloader kết nối WiFi qua ESP-01
6. Tải firmware mới từ HTTP Server (GET /update.bin)
7. Xóa vùng Flash App → Ghi firmware mới
8. Reset → Chạy firmware mới
```

### HTTP Server (OTA)

- Chạy trên PC tại port `8080`
- Phục vụ file firmware: `GET /update.bin`
- File firmware: `Code_Core/MDK-ARM/Code_Core/Code_Core.bin` (output từ Keil)

---

## 🖥️ Phần Mềm PC

### 1. Motor Monitor (`motor_monitor.py`)

Ứng dụng GUI Python giám sát realtime động cơ:

| Tính năng | Mô tả |
|:---|:---|
| **Hiển thị trạng thái** | Mode (FORWARD/REVERSE/STOP), giá trị ADC, PWM, vị trí, RPM |
| **Biểu đồ realtime** | 4 biểu đồ: ADC, PWM Duty, Position, RPM (cập nhật ~12fps) |
| **Kết nối TCP** | TCP Server lắng nghe trên port 8000, chờ ESP-01 kết nối |
| **CRC8 Verification** | Kiểm tra tính toàn vẹn mỗi gói dữ liệu |
| **Export CSV** | Xuất dữ liệu ra file CSV |
| **Auto-reconnect** | Tự động reconnect khi mất kết nối |

**Chạy từ source:**
```bash
pip install matplotlib numpy
python motor_monitor.py
```

**Hoặc chạy bản đã build:** `GUI_Tools/MotorMonitor.exe`

---

### 2. OTA Server (`ota_gui.py`)

Ứng dụng GUI Python phục vụ firmware OTA:

| Tính năng | Mô tả |
|:---|:---|
| **HTTP Server** | Lắng nghe trên port 8080 |
| **Chọn file firmware** | Tự động tìm hoặc browse file `.bin` |
| **Console Log** | Hiển thị quá trình truyền firmware |
| **Gửi firmware** | Gửi từng chunk 64 bytes với delay 10ms |

**Chạy từ source:**
```bash
python ota_gui.py
```

**Hoặc chạy bản đã build:** `GUI_Tools/OTA_Server.exe`

---

### 3. MATLAB Monitor (`Code_Core/matlab_monitor.m`)

Script MATLAB nhận dữ liệu qua TCP (dùng cho phân tích và báo cáo):

```matlab
% Chạy trong MATLAB
>> matlab_monitor
```

---

## 📂 Cấu Trúc Thư Mục

```
Code_VDKMorong/
├── App_Firmware/                   # Dự án STM32CubeMX + Keil (Application)
│   ├── Code_Core.ioc              # File cấu hình STM32CubeMX
│   ├── OTA_Bootloader/
│   │   └── bootloader_main.c      # OTA Bootloader (10KB, bare-metal)
│   ├── Core/
│   │   ├── Inc/                   # Header files
│   │   │   ├── main.h
│   │   │   ├── adc.h
│   │   │   ├── gpio.h
│   │   │   ├── tim.h
│   │   │   ├── usart.h
│   │   │   └── FreeRTOSConfig.h
│   │   └── Src/                   # Source files
│   │       ├── main.c             # Entry point, khởi tạo peripherals
│   │       ├── freertos.c         # Tất cả FreeRTOS tasks
│   │       ├── gpio.c             # Cấu hình GPIO (nút, LED, motor)
│   │       ├── adc.c              # Cấu hình ADC (đọc biến trở)
│   │       ├── tim.c              # Cấu hình Timer (PWM + Encoder)
│   │       └── usart.c            # UART + ESP-01 driver + CRC8
│   ├── Drivers/                   # HAL Drivers (STM32)
│   ├── Middlewares/               # FreeRTOS Kernel
│   ├── MDK-ARM/                   # Keil project files
│   └── matlab_monitor.m           # MATLAB TCP monitor script
│
├── Bootloader/                     # Project Bootloader riêng (Keil)
├── Firmware_Backup/                # Bản backup firmware
│
├── motor_monitor.py               # 🖥️ GUI Python — Giám sát realtime
├── ota_gui.py                     # 🖥️ GUI Python — OTA Server
│
├── GUI_Tools/                      # Bản build EXE (chạy không cần Python)
│   ├── MotorMonitor.exe           # Motor Monitor (standalone)
│   └── OTA_Server.exe             # OTA Server (standalone)
│
└── README.md                      # 📄 File này
```

---

## 🚀 Hướng Dẫn Sử Dụng

### Bước 1: Chuẩn Bị Phần Cứng

1. Kết nối STM32F103C8T6 theo sơ đồ chân ở trên
2. Nối ESP-01 với STM32 qua UART (PA9-TX, PA10-RX)
3. Nối motor driver (L298N/TB6612) với các chân điều khiển
4. Nối Encoder với PA0, PA1
5. Nối biến trở với PA2
6. Nối 3 nút nhấn điều khiển (PB0, PB10, PB11) xuống GND
7. Nối nút OTA (PB12) lên VCC
8. Nối 3 LED chỉ thị (PA4, PA5, PA7) qua điện trở 330Ω xuống GND

### Bước 2: Cấu Hình WiFi

Sửa thông số WiFi trong các file sau (nếu cần):

| File | Dòng | Thông số |
|:---|:---:|:---|
| `App_Firmware/Core/Src/freertos.c` | 435 | IP Server: `ESP_Init("172.20.10.7", 8000)` |
| `App_Firmware/Core/Src/usart.c` | 127 | WiFi: `AT+CWJAP="tuikycuc","18032005"` |
| `App_Firmware/OTA_Bootloader/bootloader_main.c` | 22-26 | WiFi + OTA Server config |

### Bước 3: Nạp Firmware

1. Mở project `App_Firmware/MDK-ARM/Code_Core.uvprojx` bằng **Keil MDK-ARM**
2. Build project (F7)
3. Nạp Bootloader trước (nếu cần OTA): Target Flash `0x08000000`
4. Nạp Application: Target Flash `0x08002800`
5. Hoặc nạp firmware đầy đủ qua ST-Link / SWD

### Bước 4: Chạy Phần Mềm Giám Sát

**Cách 1: Chạy file EXE (không cần cài Python)**
```
Chạy file: GUI_Tools/MotorMonitor.exe
```

**Cách 2: Chạy từ source**
```bash
pip install matplotlib numpy
python motor_monitor.py
```

1. Nhấn **Connect** để bắt đầu lắng nghe (mặc định port 8000)
2. Bật nguồn STM32 → ESP-01 tự động kết nối WiFi → Kết nối TCP đến PC
3. Dữ liệu sẽ hiển thị realtime trên giao diện

### Bước 5: Sử Dụng OTA (Tùy chọn)

1. Chạy `GUI_Tools/OTA_Server.exe` hoặc `python ota_gui.py`
2. Chọn file firmware `.bin` (hoặc tự động tìm trong `MDK-ARM/Code_Core/`)
3. Nhấn **START SERVER** (port 8080)
4. Trên board STM32: **nhấn giữ nút PB12 trong 3 giây**
5. LED nhấp nháy → MCU reset → Tải firmware mới → Reset và chạy

---

## 📋 Yêu Cầu Phần Mềm

### Phát triển Firmware
- **Keil MDK-ARM v5** (hoặc tương đương)
- **STM32CubeMX** v6.15+
- **STM32Cube FW_F1** v1.8.7

### Phần Mềm PC
- **Python 3.8+** (nếu chạy từ source)
- **matplotlib** — Biểu đồ realtime
- **numpy** — Xử lý dữ liệu
- **tkinter** — GUI (đi kèm Python)

### Hoặc dùng bản đã build
- `GUI_Tools/MotorMonitor.exe` — Không cần cài Python
- `GUI_Tools/OTA_Server.exe` — Không cần cài Python

---

## ⚠️ Lưu Ý Quan Trọng

1. **Địa chỉ IP PC** phải khớp với IP được ghi trong code STM32 (`172.20.10.7`)
2. **WiFi SSID/Password** phải đúng (`tuikycuc` / `18032005`)
3. **ESP-01** cần được cấp nguồn ổn định 3.3V (tối thiểu 200mA)
4. Khi đổi hướng quay motor, hệ thống tự động chờ motor dừng hẳn để bảo vệ
5. **CPR Encoder** (`cpr = 1562`) cần chỉnh lại nếu dùng encoder khác
6. Firewall trên PC cần cho phép kết nối TCP vào port 8000 và 8080

---

> 📌 **Tác giả**: Đồ án Vi Điều Khiển - Bui Huy Hoang
> 📌 **MCU**: STM32F103C8T6 | **RTOS**: FreeRTOS | **WiFi**: ESP-01 (AT Command)
