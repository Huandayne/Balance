#include <WiFi.h>
#include <ESPmDNS.h>
#include "HX711.h"
#include "BluetoothSerial.h"
#include <esp_task_wdt.h>

#define WDT_TIMEOUT_MS  10000 
#define SIMULATE        false  
#define UPDATE_MS       200    
#define NO_LOAD_THRESHOLD   0.5f 
#define BT_DEVICE_NAME "ESP32_Can_Bang" 

// --- CẤU HÌNH WIFI (CHẾ ĐỘ STA - KẾT NỐI ROUTER) ---
#define WIFI_SSID       "YOUR_WIFI_SSID"      // Nhập tên WiFi router của bạn
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"  // Nhập mật khẩu WiFi
#define TCP_PORT        8888
#define MDNS_NAME       "esp32-balance"

BluetoothSerial SerialBT;
WiFiServer wifiServer(TCP_PORT);
WiFiClient wifiClient;

const float DECK_W_CM = 45.5f;
const float DECK_H_CM = 45.5f;

const float HALF_W = DECK_W_CM / 2.0f;
const float HALF_H = DECK_H_CM / 2.0f;

const int HX_DT1 = 4;
const int HX_SCK1 = 16;

const int HX_DT2 = 17;
const int HX_SCK2 = 5;

const int HX_DT3 = 18;
const int HX_SCK3 = 19;

const int HX_DT4 = 32;
const int HX_SCK4 = 33;

HX711 hx1, hx2, hx3, hx4;

float scaleFactor[4] = { 
  46720.0 / 9.81,
  47800.0 / 9.81,
  46740.0 / 9.81,
  46700.0 / 9.81
};

unsigned long lastUpdate = 0;
char jsonBuffer[200]; 

void safeInit(HX711 &hx, int dt, int sck, float factor) {
  hx.begin(dt, sck);
  hx.set_scale(factor);
  
  unsigned long t = millis();
  while (!hx.is_ready() && millis() - t < 200) { 
      delay(10);
  }
  
  if (hx.is_ready()) hx.tare();
}

float readForce(HX711& h) {
    if (!h.is_ready()) return 0.0f; 
    float val = h.get_units(1);
    return (val < 0) ? 0.0f : val;
}

static void computeCOP(float F1, float F2, float F3, float F4, float Ftot, float& outX, float& outY) {
    if (Ftot <= 0) { outX = 0; outY = 0; return; }
    
    float Right = F2 + F4;
    float Left  = F1 + F3;
    float Top   = F1 + F2;
    float Bot   = F3 + F4;
    
    outX = ((Right - Left) / Ftot) * HALF_W;
    outY = ((Top - Bot) / Ftot) * HALF_H;
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n--- BALANCE SYSTEM STARTING ---");
    
    // 1. Khởi tạo Bluetooth Serial
    SerialBT.begin(BT_DEVICE_NAME); 
    Serial.printf("[Bluetooth] Ready as: %s\n", BT_DEVICE_NAME);

    // 2. Khởi tạo WiFi chế độ STA (kết nối router)
    Serial.printf("[WiFi] Connecting to: %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 8000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("\n[WiFi] Connected! IP: ");
        Serial.println(WiFi.localIP());

        // Đăng ký mDNS để app có thể tìm qua "esp32-balance.local"
        if (MDNS.begin(MDNS_NAME)) {
            Serial.printf("[mDNS] Started: %s.local (Port %d)\n", MDNS_NAME, TCP_PORT);
            MDNS.addService("balance", "tcp", TCP_PORT);
        }

        wifiServer.begin();
        Serial.printf("[TCP Server] Listening on port %d\n", TCP_PORT);
    } else {
        Serial.println("\n[WiFi] Connection timeout. Bluetooth & USB Serial remain operational.");
    }

    // 3. Khởi tạo Watchdog sau khi đã kết nối mạng để tránh timeout
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_MS,
        .idle_core_mask = (1 << 0),
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL); 

    // 4. Khởi tạo cảm biến
    if (!SIMULATE) {
        safeInit(hx1, HX_DT1, HX_SCK1, scaleFactor[0]);
        safeInit(hx2, HX_DT2, HX_SCK2, scaleFactor[1]);
        safeInit(hx3, HX_DT3, HX_SCK3, scaleFactor[2]);
        safeInit(hx4, HX_DT4, HX_SCK4, scaleFactor[3]);
    }
    Serial.println("--- SYSTEM READY ---");
}

void loop() {
    esp_task_wdt_reset();

    // Tiếp nhận kết nối WiFi client mới
    if (WiFi.status() == WL_CONNECTED) {
        if (wifiServer.hasClient()) {
            if (!wifiClient || !wifiClient.connected()) {
                wifiClient = wifiServer.available();
                Serial.println("[WiFi TCP] BalanceApp connected!");
            } else {
                WiFiClient rejected = wifiServer.available();
                rejected.stop();
            }
        }
    }

    unsigned long now = millis();
    if (now - lastUpdate < UPDATE_MS) return;
    lastUpdate = now;

    float F1, F2, F3, F4; 

    if (SIMULATE) {
        F1 = random(10, 100); 
        F2 = random(10, 100);
        F3 = random(10, 100); 
        F4 = random(10, 100);
    } else {
        F1 = readForce(hx1); 
        F2 = readForce(hx2);
        F3 = readForce(hx3); 
        F4 = readForce(hx4);
    }

    float Ftot = F1 + F2 + F3 + F4;
    bool noLoad = (Ftot < NO_LOAD_THRESHOLD);

    float X_cm = 0, Y_cm = 0;
    if (!noLoad) {
        computeCOP(F1, F2, F3, F4, Ftot, X_cm, Y_cm);
    }

    snprintf(jsonBuffer, sizeof(jsonBuffer), 
             "{\"ts\":%lu,\"f1\":%.2f,\"f2\":%.2f,\"f3\":%.2f,\"f4\":%.2f,\"sum\":%.2f,\"x\":%.2f,\"y\":%.2f}",
             now, F1, F2, F3, F4, Ftot, X_cm, Y_cm);

    // 1. Luôn gửi ra USB Serial
    Serial.println(jsonBuffer);   
    
    // 2. Gửi ra Bluetooth nếu có thiết bị kết nối
    if (SerialBT.hasClient()) { 
        SerialBT.println(jsonBuffer); 
    }

    // 3. Gửi ra WiFi TCP Socket nếu có thiết bị kết nối
    if (wifiClient && wifiClient.connected()) {
        wifiClient.println(jsonBuffer);
    }
}
