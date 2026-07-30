// ================= ================= ================= =================
// 1. NHẬP CÁC THƯ VIỆN CẦN THIẾT
// ================= ================= ================= =================

#include "HX711.h"            // Thư viện để làm việc với mô-đun giải mã tín hiệu HX711 (dùng cho Loadcell/Cảm biến lực)
#include "BluetoothSerial.h"  // Thư viện hỗ trợ giao tiếp Bluetooth Classic trên vi điều khiển ESP32
#include <esp_task_wdt.h>     // Thư viện điều khiển Watchdog Timer (WDT) cấp hệ thống của ESP32 để chống treo vi điều khiển


// ================= ================= ================= =================
// 2. CẤU HÌNH HỆ THỐNG VÀ HẰNG SỐ (CONFIGURATION)
// ================= ================= ================= =================

// Thời gian chờ của Watchdog tính bằng mili-giây (10000 ms = 10 giây).
// Nếu chương trình bị treo quá 10 giây không báo lại với WDT, chip ESP32 sẽ tự động khởi động lại (Reset).
#define WDT_TIMEOUT_MS  10000 

// Chế độ mô phỏng dữ liệu:
// - true : Hệ thống tự tạo số ngẫu nhiên (không cần cắm thực tế các cảm biến HX711).
// - false: Đọc dữ liệu thực tế từ 4 cảm biến HX711 cắm vào phần cứng.
#define SIMULATE        false  

// Chu kỳ cập nhật và gửi dữ liệu qua Serial/Bluetooth (200ms = 5 lần/giây).
#define UPDATE_MS       200    

// Ngưỡng phát hiện "Không có tải" (đơn vị cùng loại với Ftot, ví dụ: kg hoặc N).
// Nếu tổng lực Ftot nhỏ hơn 0.5, hệ thống hiểu là không có người/vật nằm trên bàn cân.
#define NO_LOAD_THRESHOLD   0.5f 

// Tên đại diện của thiết bị Bluetooth khi phát ra xung quanh để điện thoại/máy tính dò tìm.
#define BT_DEVICE_NAME "ESP32_Can_Bang" 


// ================= ================= ================= =================
// 3. ĐỊNH NGHĨA BIẾN TOÀN CỤC VÀ TỌA ĐỘ BÀN CÂN
// ================= ================= ================= =================

// Khởi tạo đối tượng `SerialBT` quản lý các tác vụ truyền nhận qua Bluetooth SPP (Serial Port Profile).
BluetoothSerial SerialBT;

// Kích thước của mặt bàn cân hình chữ nhật (Chiều rộng W và Chiều cao H) tính bằng centimet (cm).
const float DECK_W_CM = 45.5f; // Chiều rộng tổng cộng = 45.5 cm
const float DECK_H_CM = 45.5f; // Chiều cao tổng cộng = 45.5 cm

// Tính khoảng cách từ tâm bàn cân (gốc tọa độ 0,0) ra các mép viền.
// Tọa độ X sẽ chạy từ [-HALF_W đến +HALF_W], Y chạy từ [-HALF_H đến +HALF_H].
const float HALF_W = DECK_W_CM / 2.0f; // Bán kính theo chiều ngang = 22.75 cm
const float HALF_H = DECK_H_CM / 2.0f; // Bán kính theo chiều dọc = 22.75 cm


// ================= ================= ================= =================
// 4. PHÂN BỔ CHÂN GPIO CHO 4 CẢM BIẾN HX711
// ================= ================= ================= =================
// Mỗi mô-đun HX711 cần 2 chân truyền tín hiệu: DT (Data) và SCK (Serial Clock)

// Cảm biến 1 (Thường đặt ở vị trí: Góc Trên - Bên Trái)
const int HX_DT1 = 4;   // Chân dữ liệu Data 1 nối vào GPIO4
const int HX_SCK1 = 16; // Chân xung Clock 1 nối vào GPIO16

// Cảm biến 2 (Thường đặt ở vị trí: Góc Trên - Bên Phải)
const int HX_DT2 = 17;  // Chân dữ liệu Data 2 nối vào GPIO17
const int HX_SCK2 = 5;  // Chân xung Clock 2 nối vào GPIO5

// Cảm biến 3 (Thường đặt ở vị trí: Góc Dưới - Bên Trái)
const int HX_DT3 = 18;  // Chân dữ liệu Data 3 nối vào GPIO18
const int HX_SCK3 = 19; // Chân xung Clock 3 nối vào GPIO19

// Cảm biến 4 (Thường đặt ở vị trí: Góc Dưới - Bên Phải)
const int HX_DT4 = 32;  // Chân dữ liệu Data 4 nối vào GPIO32
const int HX_SCK4 = 33; // Chân xung Clock 4 nối vào GPIO33

// Khởi tạo 4 đối tượng quản lý phần cứng HX711 từ thư viện
HX711 hx1, hx2, hx3, hx4;

// Mảng chứa hệ số hiệu chuẩn (Scale Factor) của từng cảm biến.
// Công thức: (Giá trị Raw khi đặt tải chuẩn) / (Giá trị Trọng lượng chuẩn).
// Ở đây dùng tỉ lệ theo gia tốc trọng trường 9.81 m/s² để quy đổi lực.
float scaleFactor[4] = { 
  46720.0 / 9.81, // Hệ số cân chỉnh cho Cảm biến 1
  47800.0 / 9.81, // Hệ số cân chỉnh cho Cảm biến 2
  46740.0 / 9.81, // Hệ số cân chỉnh cho Cảm biến 3
  46700.0 / 9.81  // Hệ số cân chỉnh cho Cảm biến 4
};

// Biến lưu mốc thời gian (tính bằng mili-giây) của lần gửi dữ liệu gần nhất, phục vụ tính chu kỳ UPDATE_MS.
unsigned long lastUpdate = 0;

// Mảng ký tự (chuỗi) dùng làm bộ đệm chứa câu JSON output hoàn chỉnh trước khi gửi qua Serial/Bluetooth.
char jsonBuffer[200]; 


// ================= ================= ================= =================
// 5. CÁC HÀM BỔ TRỢ (HELPER FUNCTIONS)
// ================= ================= ================= =================

/**
 * @brief Hàm khởi tạo an toàn cho mô-đun HX711, tránh treo ESP32 nếu cảm biến bị đứt dây.
 * @param hx Cấu trúc tham chiếu đến đối tượng HX711 cần khởi tạo.
 * @param dt Chân GPIO Data.
 * @param sck Chân GPIO Clock.
 * @param factor Hệ số scale chuyển đổi lực.
 */
void safeInit(HX711 &hx, int dt, int sck, float factor) {
  hx.begin(dt, sck);      // Thiết lập chân phần cứng kết nối với HX711
  hx.set_scale(factor);   // Đặt hệ số scale chuyển đổi
  
  unsigned long t = millis(); // Lấy thời gian bắt đầu chờ
  
  // Vòng lặp chờ cảm biến sẵn sàng (is_ready).
  // Giới hạn tối đa trong 200ms. Nếu hỏng dây/mất kết nối, vòng lặp dừng lại để không làm treo toàn bộ vi điều khiển.
  while (!hx.is_ready() && millis() - t < 200) { 
      delay(10); // Tạm dừng 10ms mỗi chu kỳ chờ
  }
  
  // Nếu cảm biến phản hồi tốt, tiến hành Tare (đặt điểm 0 - trừ đi khối lượng bản thân của mặt bàn cân)
  if (hx.is_ready()) hx.tare();
}

/**
 * @brief Hàm đọc giá trị lực an toàn từ 1 cảm biến HX711.
 * @param h Tham chiếu đến cảm biến cần đọc.
 * @return Giá trị lực đọc được (không trả về số âm).
 */
float readForce(HX711& h) {
    // Nếu cảm biến bận hoặc mất kết nối -> Trả về 0.0 lập tức để bảo vệ luồng chương trình
    if (!h.is_ready()) return 0.0f; 
    
    // Lấy giá trị thực tế đã qua hiệu chuẩn scale (lấy 1 mẫu duy nhất để tối ưu tốc độ đọc)
    float val = h.get_units(1);
    
    // Nếu lực đọc ra bé hơn 0 (do nhiễu hoặc lệch điểm Tare), tự động ép về 0.0
    return (val < 0) ? 0.0f : val;
}

/**
 * @brief Hàm tính toán Tọa độ Áp lực Trung tâm (COP - Center of Pressure).
 * @param F1 Lực chân 1 (Trái - Trên)
 * @param F2 Lực chân 2 (Phải - Trên)
 * @param F3 Lực chân 3 (Trái - Dưới)
 * @param F4 Lực chân 4 (Phải - Dưới)
 * @param Ftot Tổng lực của cả 4 chân
 * @param outX Biến nhận kết quả Tọa độ X (Được truyền theo tham chiếu &)
 * @param outY Biến nhận kết quả Tọa độ Y (Được truyền theo tham chiếu &)
 */
static void computeCOP(float F1, float F2, float F3, float F4, float Ftot, float& outX, float& outY) {
    // Nếu tổng lực <= 0 (không có tải), trả về tọa độ gốc (0, 0) và thoát hàm
    if (Ftot <= 0) { outX = 0; outY = 0; return; }
    
    // Tính tổng lực đè lên phía Bên Phải (Cảm biến 2 và 4)
    float Right = F2 + F4;
    // Tính tổng lực đè lên phía Bên Trái (Cảm biến 1 và 3)
    float Left  = F1 + F3;
    // Tính tổng lực đè lên phía Bên Trên (Cảm biến 1 và 2)
    float Top   = F1 + F2;
    // Tính tổng lực đè lên phía Bên Dưới (Cảm biến 3 và 4)
    float Bot   = F3 + F4;
    
    // Công thức tính tọa độ X (-HALF_W đến +HALF_W):
    // Tỉ lệ lệch lực giữa Phải và Trái nhân với bán kính chiều rộng
    outX = ((Right - Left) / Ftot) * HALF_W;
    
    // Công thức tính tọa độ Y (-HALF_H đến +HALF_H):
    // Tỉ lệ lệch lực giữa Trên và Dưới nhân với bán kính chiều cao
    outY = ((Top - Bot) / Ftot) * HALF_H;
}


// ================= ================= ================= =================
// 6. HÀM KHỞI TẠO HỆ THỐNG (SETUP) - CHẠY 1 LẦN DUY NHẤT KHI BẬT NGUỒN
// ================= ================= ================= =================

void setup() {
    // Mở cổng giao tiếp Serial qua cáp USB với tốc độ 115200 baud để debug trên máy tính
    Serial.begin(115200);
    
    // --- KHỞI TẠO WATCHDOG TIMER (Cho phiên bản ESP32 Arduino Core V3.x) ---
    
    // Cấu hình các tham số cho Watchdog
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_MS, // Cài đặt thời gian chờ tối đa (10000ms)
        .idle_core_mask = (1 << 0),   // Theo dõi tiến trình rảnh trên Core 0 (Nhân 0)
        .trigger_panic = true         // Khi vi điều khiển bị gác/treo -> Kích hoạt Panic (Tự Reset phần cứng)
    };
    
    // Khởi tạo Watchdog với cấu hình ở trên
    esp_task_wdt_init(&wdt_config);
    
    // Thêm Task hiện tại (Chính là hàm loop()) vào danh sách Watchdog giám sát
    esp_task_wdt_add(NULL); 
    // ----------------------------------------------------

    // Bật phát sóng Bluetooth Classic với tên đã đặt ở trên
    SerialBT.begin(BT_DEVICE_NAME); 
    
    // In ra màn hình Serial máy tính báo hiệu hệ thống đã khởi động xong
    Serial.println("--- SYSTEM START ---");

    // Nếu không bật chế độ mô phỏng (SIMULATE = false), tiến hành khởi tạo 4 cảm biến HX711 thực tế
    if (!SIMULATE) {
        safeInit(hx1, HX_DT1, HX_SCK1, scaleFactor[0]); // Khởi tạo HX711 số 1
        safeInit(hx2, HX_DT2, HX_SCK2, scaleFactor[1]); // Khởi tạo HX711 số 2
        safeInit(hx3, HX_DT3, HX_SCK3, scaleFactor[2]); // Khởi tạo HX711 số 3
        safeInit(hx4, HX_DT4, HX_SCK4, scaleFactor[3]); // Khởi tạo HX711 số 4
    }
}


// ================= ================= ================= =================
// 7. HÀM LẶP VÔ TẬN (LOOP) - CHẠY LIÊN TỤC KHI THIẾT BỊ HOẠT ĐỘNG
// ================= ================= ================= =================

void loop() {
    // Báo hiệu cho Watchdog biết rằng: "Chương trình vẫn đang chạy bình thường, chưa bị treo!"
    // Nếu không gọi hàm này trong quá 10 giây, WDT sẽ ra lệnh Reset chip.
    esp_task_wdt_reset();

    // Lấy thời gian hiện tại từ lúc chip bật (tính theo ms)
    unsigned long now = millis();
    
    // Kiểm tra xem đã đủ thời gian chu kỳ cập nhật dữ liệu (UPDATE_MS = 200ms) chưa.
    // Nếu chưa đủ 200ms -> Bỏ qua phần còn lại của loop(), chờ vòng lặp sau.
    if (now - lastUpdate < UPDATE_MS) return;
    
    // Đã đủ 200ms -> Cập nhật mốc thời gian mới
    lastUpdate = now;

    // Khai báo các biến lưu giá trị lực của 4 góc
    float F1, F2, F3, F4; 

    // Kiểm tra chế độ hoạt động
    if (SIMULATE) {
        // Nếu SIMULATE = true: Tự động sinh số ngẫu nhiên từ 10 đến 100 để test giao diện/thuật toán
        F1 = random(10, 100); 
        F2 = random(10, 100);
        F3 = random(10, 100); 
        F4 = random(10, 100);
    } else {
        // Nếu SIMULATE = false: Đọc lực thực tế từ 4 cảm biến phần cứng
        F1 = readForce(hx1); 
        F2 = readForce(hx2);
        F3 = readForce(hx3); 
        F4 = readForce(hx4);
    }

    // Tính tổng lực đè lên toàn bộ mặt bàn cân
    float Ftot = F1 + F2 + F3 + F4;
    
    // Kiểm tra xem tổng lực có nhỏ hơn ngưỡng cài đặt "Không tải" hay không
    bool noLoad = (Ftot < NO_LOAD_THRESHOLD);

    // Khai báo biến lưu vị trí tọa độ X và Y của lực trung tâm (COP)
    float X_cm = 0, Y_cm = 0;
    
    // Nếu có tải đè lên bàn cân -> Mới tiến hành tính toán tọa độ COP
    if (!noLoad) {
        computeCOP(F1, F2, F3, F4, Ftot, X_cm, Y_cm);
    }

    // Đóng gói tất cả các thông số thu thập được vào một chuỗi dạng chuẩn JSON.
    // Dạng JSON xuất ra: {"ts":... , "f1":... , "f2":... , "f3":... , "f4":... , "sum":... , "x":... , "y":...}
    snprintf(jsonBuffer, sizeof(jsonBuffer), 
             "{\"ts\":%lu,\"f1\":%.2f,\"f2\":%.2f,\"f3\":%.2f,\"f4\":%.2f,\"sum\":%.2f,\"x\":%.2f,\"y\":%.2f}",
             now, F1, F2, F3, F4, Ftot, X_cm, Y_cm);

    // Gửi chuỗi JSON qua cổng Serial USB lên máy tính
    Serial.println(jsonBuffer);   
    
    // Kiểm tra xem hiện tại có thiết bị nào (điện thoại/máy tính) đang kết nối Bluetooth với ESP32 hay không.
    if (SerialBT.hasClient()) { 
        // Nếu có kết nối -> Gửi chuỗi JSON qua sóng Bluetooth
        SerialBT.println(jsonBuffer); 
    }
}
