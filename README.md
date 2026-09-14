# Dự án: Hệ Thống Đo Lực Cân Bằng (NewBalanceApp)

Dự án này là một hệ thống IoT kết hợp ứng dụng Desktop (App) để đo đạc và phân tích tâm áp lực (Center of Pressure - COP) của người dùng hoặc bệnh nhân, dùng trong y tế hoặc thể thao. Hệ thống bao gồm phần cứng (ESP32 + 4 cảm biến loadcell HX711) và phần mềm C# (Avalonia UI).

## 1. Kiến Trúc Hệ Thống (Architecture)

Hệ thống được chia thành 2 phần chính:

### Một. Phần cứng (Firmware) - Nằm trong thư mục `arduino/`
- **Vi điều khiển**: ESP32 (Hỗ trợ Dual-Core và Bluetooth/WiFi).
- **Cảm biến**: 4 module HX711 kết nối với 4 Loadcell đặt ở 4 góc của bàn cân.
- **Mã nguồn chính**: `arduino.ino`
- **Hoạt động**:
  - Đọc tín hiệu lực từ 4 góc (F1, F2, F3, F4) thông qua thư viện `HX711.h`.
  - Tính toán tổng lực (sum) và tọa độ tâm áp lực (X, Y).
  - Đóng gói dữ liệu thành một chuỗi JSON.
  - Liên tục gửi chuỗi JSON này (ở tần số ~5Hz/200ms) đồng thời qua:
    - Cổng **Serial (USB)**: 115200 baud.
    - **Bluetooth Serial**: Tên thiết bị `ESP32_Can_Bang`.
    - **WiFi (Chế độ STA - kết nối router)**: Mở TCP Server cổng `8888`, hỗ trợ mDNS với tên `esp32-balance.local`.
  - Có tích hợp Watchdog (`esp_task_wdt`) để tự động khởi động lại nếu bị treo.

### Hai. Phần mềm Desktop (App) - Nằm trong thư mục `BalanceApp/`
- **Framework**: .NET 9.0 + Avalonia UI (Cross-platform GUI).
- **Kiến trúc code**: MVVM (Model-View-ViewModel) với `CommunityToolkit.Mvvm`.
- **Thư viện vẽ đồ thị**: `ScottPlot` (vẽ biểu đồ tâm áp lực COP theo thời gian thực).
- **Database**: SQLite qua `Microsoft.EntityFrameworkCore` (EF Core).
- **Hoạt động**:
  - **Kết nối đa kênh**: `DualSensorService.cs` quản lý linh hoạt giữa `WifiSensorService.cs` (kết nối TCP Socket qua WiFi) và `SerialSensorService.cs` (kết nối Bluetooth / COM Port). Hỗ trợ chọn chế độ **WiFi**, **Bluetooth**, hoặc **chạy đồng thời cả 2 kênh (Dual-link)** kèm cơ chế khử trùng lặp dữ liệu theo timestamp.
  - **Xử lý luồng dữ liệu (Real-time)**: Dữ liệu bay vào được đẩy lên `MeasurementViewModel.cs`, từ đó kích hoạt view (`MeasurementView.axaml.cs`) thông qua `UpdateGraphAction` để vẽ quỹ đạo điểm COP dời đổi trên biểu đồ ScottPlot dạng Scatter.
  - **Lưu trữ**: Khi bấm "BẮT ĐẦU ĐO", app bắt đầu đệm dữ liệu vào RAM. Khi bấm "DỪNG ĐO", app dùng `TestSessionService.cs` gom các mẫu đo lại và chèn vào Database (vào bảng `TestSessions` và `TestSamples`).

---

## 2. Cấu trúc Thư Mục (Directory Structure)

```text
d:\Code\NewBalanceApp\
├── arduino/
│   └── arduino.ino          // Firmware cho ESP32 (Mở bằng Arduino IDE)
├── database_creation.sql    // Script SQL thô tạo database (Tham khảo)
├── BalanceApp/              // Mã nguồn C# Avalonia UI App
│   ├── Assets/              // Hình ảnh, icon (logo, v.v..)
│   ├── Models/              // Định nghĩa các bảng Database (Patient, TestSession, TestSample, User)
│   ├── Migrations/          // Chứa các file Migration của EF Core (Tạo/Cập nhật DB)
│   ├── Services/
│   │   ├── BalanceDbContext.cs       // Cấu hình Database SQLite
│   │   ├── PatientService.cs         // Thao tác DB (CRUD) với Bệnh nhân
│   │   ├── TestSessionService.cs     // Thao tác DB để lưu lịch sử Đo
│   │   └── Sensor/
│   │       ├── ISensorService.cs     // Interface quy chuẩn cảm biến
│   │       └── SerialSensorService.cs // Code đọc Serial COM (JSON) từ ESP
│   ├── ViewModels/          // Chứa Logic cho giao diện (MVVM)
│   │   ├── MainViewModel.cs          // Điều hướng chính
│   │   └── MeasurementViewModel.cs   // Điều khiển đo đạc, start/stop, dữ liệu biểu đồ
│   └── Views/               // Giao diện người dùng (.axaml)
│       └── MeasurementView.axaml.cs  // Chứa logic vẽ biểu đồ ScottPlot thực tế
└── BalanceApp.slnx          // File Solution mới nhất của .NET
```

---

## 3. Các thành phần Database cốt lõi (Entity Core)

Các bảng chính trong CSDL SQLite (tạo thông qua Code-First Entity Framework):
1. **Users**: Lưu thông tin bác sĩ / người quản trị (Đăng nhập).
2. **Patients**: Lưu thông tin người dùng / bệnh nhân (Mã y tế, Tên, Tuổi, Cân nặng...).
3. **TestSessions**: Ghi nhận một "phiên đo" (Thời gian đo, thuộc bệnh nhân nào, giá trị X/Y trung bình, ghi chú).
4. **TestSamples**: Chi tiết từng điểm lấy mẫu trong lúc đo đạc (~20ms-50ms sinh ra 1 row) của một `TestSession`. Chứa lực `F1, F2, F3, F4` và tung độ/hoành độ `X, Y`.

---

## 4. Hướng dẫn thiết lập (Setup & Build)

### 4.1. Đối với phần cứng (ESP32)
1. Cài đặt **Arduino IDE** và cài board **ESP32** (từ Espressif).
2. Tải và cài đặt thư viện `HX711 Arduino Library` (của bodge) qua Library Manager.
3. Mở file `arduino/arduino.ino`.
4. Xem và đổi chân kết nối ở các dòng: `const int HX_DT1 = 4; const int HX_SCK1 = 16; ...` cho phù hợp hệ thống dây thực tế.
5. Cắm ESP32 vào PC, chọn cổng COM và bấm **Upload**.

*Mẹo: Nếu không có phần cứng, có thể đổi cờ `#define SIMULATE false` thành `#define SIMULATE true` trên dòng 7 của file INO để chip tự sinh tọa độ mô phỏng qua Serial.*

### 4.2. Đối với phần mềm (Balance App)
1. Yêu cầu tải / Cài đặt **.NET 9.0 SDK**.
2. IDE khuyên dùng: **Visual Studio 2022**, **Rider**, hoặc **VS Code** (kèm Avalonia extension).
3. Mở terminal tại thư mục `BalanceApp/`:
   ```bash
   dotnet restore
   dotnet build
   dotnet run
   ```
4. Database sẽ tự động sinh file `balance_app.db` ở thư mục chạy (nhờ `DataSeeder.cs` và `EnsureCreated`).

---

## 5. Bàn giao thao tác cho người tiếp theo (Next Steps)

1. **Hiệu chuẩn cảm biến (Calibration)**:
   Hiện tại, biến `scaleFactor` đang được hardcode trong `arduino.ino` ở Dòng 27 (`46720.0 / 9.81` ...). Người được bàn giao cần làm một hệ thống/menu trên app hoặc tool rời để hiệu chuẩn lại 4 góc tải khi đổi Loadcell mới rồi nạp xuống ESP bảo lưu vào EEPROM (hoặc Preferences). 
2. **Cập nhật giao diện Data Visualization**: Thư viện ScottPlot đang hoạt động tốt. Tuy nhiên trong `MeasurementView.axaml.cs` có setup các buffer `_historyX`, `_activeX`. Nếu đo quá lâu (trên 10 phút) RAM sẽ tăng rất cao, cần tối ưu cơ chế xoay vòng dữ liệu mảng (Circular Buffer) cho biểu đồ.
3. **Phân quyền người dùng**: Hiện đã có bảng Users ở Database, nhưng App chủ yếu đang bypass login, cần viết View và ViewModel đăng nhập riêng tư. 

---

