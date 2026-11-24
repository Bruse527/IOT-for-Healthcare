// =============================================================
// HeartCare_TestSketch - ESP32 醫療監護系統核心韌體 
// 腳位設計：雙 I2C Bus、ADC1 穩定性、避免敏感腳位
// -------------------------------------------------------------
//非阻塞狀態機, 雙 I2C 總線管理 (OLED/MAX30100)
// =============================================================

// --- 庫引入 ---
#include <Wire.h> // I2C 通訊庫
#include <Adafruit_SSD1306.h> // OLED 顯示器庫
#include <MAX30100_library.h> // 假設已安裝血氧感測器庫
#include <DHT.h> // DHT22 溫濕度感測器庫

// ------------------------------------------------------------
// 1. HARDWARE PIN DEFINITIONS (硬體腳位定義)
// ------------------------------------------------------------

// --- I2C Bus 1: OLED Display (GPIO 21/22) ---
const int OLED_SDA_PIN = 21; 
const int OLED_SCL_PIN = 22; 

// --- I2C Bus 2: MAX30100 Sensor (GPIO 18/19) ---
const int MAX30100_SDA_PIN = 19; 
const int MAX30100_SCL_PIN = 18; 

// --- Digital Input/Output Pins ---
const int DHT22_PIN = 14;      // DHT22 溫濕度感測器 (GPIO14)
const int PHOTO_SENSITIVE_PIN = 34; // 光敏二極體 (ADC1 穩定輸入)
const int HCSR04_TRIG_PIN = 16;  // HC-SR04 發射
const int HCSR04_ECHO_PIN = 17;  // HC-SR04 接收
const int BUTTON_PIN = 27;      // 使用者互動按鈕 (GPIO27)
const int BEEP_PIN = 25;        // 蜂鳴器提醒 (GPIO25)

// ------------------------------------------------------------
// 2. GLOBAL VARIABLES & OBJECTS (全局變數與物件)
// ------------------------------------------------------------

// --- 系統狀態機 (Finite State Machine) ---
enum SystemState {
    STATE_IDLE,          // 正常監測狀態
    STATE_ALERT,         // 異常警報狀態
    STATE_CONFIRM_SAFE   // 平安確認狀態
};
SystemState currentState = STATE_IDLE;

// --- Sensor/Display Objects (已實作 OLED) ---
// 假設 OLED 尺寸為 128x64，I2C 地址為 0x3C
Adafruit_SSD1306 display(128, 64, &Wire, -1); 
DHT dht(DHT22_PIN, DHT22);
MAX30100 max30100;

// --- Time Management (非阻塞延遲) ---
unsigned long last_sensor_read_time = 0;
const long SENSOR_READ_INTERVAL = 2000; // 每 2 秒採集一次數據

// =============================================================
// 3. SETUP FUNCTION (初始化)
// =============================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n========================================");
    Serial.println("  HeartCare Monitor System Initializing...");
    Serial.println("========================================");

    // 1. 初始化 I2C 總線 1 (OLED)
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    
    // 初始化 OLED 顯示器
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F("SSD1306 allocation failed"));
        for(;;); // 停止運行
    }
    display.display(); // 顯示啟動畫面
    
    // 2. 初始化 I2C 總線 2 (MAX30100)
    // NOTE: 實際應用中，請確保 MAX30100 庫支持通過 SDA/SCL Pin 初始化。
    
    // 3. 初始化輸入腳位
    pinMode(BUTTON_PIN, INPUT_PULLUP); // 按鈕使用內建上拉電阻
    pinMode(DHT22_PIN, INPUT);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    
    // 4. 初始化輸出腳位
    pinMode(BEEP_PIN, OUTPUT);
    
    // 5. 初始系統狀態設置
    currentState = STATE_IDLE;
    displayStatus(currentState); // 顯示初始狀態
    Serial.println("Initializing Complete. System is IDLE.");
}

// =============================================================
// 4. MAIN LOOP FUNCTION (主迴圈 - 非阻塞)
// =============================================================

void loop() {
    // --- A. 讀取使用者互動 (非阻塞) ---
    bool button_pressed = (digitalRead(BUTTON_PIN) == LOW);
    if (button_pressed && currentState == STATE_ALERT) {
        currentState = STATE_CONFIRM_SAFE;
        Serial.println(">> Button Pressed: Entering CONFIRM_SAFE state.");
    }

    // --- B. 數據採集 (周期性檢查) ---
    unsigned long current_time = millis();
    if (current_time - last_sensor_read_time >= SENSOR_READ_INTERVAL) {
        last_sensor_read_time = current_time;
        
        // 1. 採集所有感測器數據
        float light_value = readPhotoSensor(); 
        float temperature = readDHT22();
        int distance = readUltrasound();

        // 2. 狀態邏輯判斷 (核心業務邏輯)
        checkSystemState(light_value, temperature, distance);
    }
    
    // --- C. 執行動作 (Action/Actuator Control) ---
    actuateSystem();

    delay(10); 
}

// =============================================================
// 5. HELPER FUNCTIONS (輔助函式)
// =============================================================

// 讀取光敏二極體 (使用 GPIO 34 - ADC1)
float readPhotoSensor() {
    int raw_value = analogRead(PHOTO_SENSITIVE_PIN);
    // 轉換成電壓或相對亮度值 (ADC 範圍 0-4095)
    float brightness = raw_value * (3.3 / 4095.0); 
    return brightness;
}

// 讀取 DHT22 (Placeholder)
float readDHT22() {
    // 實際：需要使用 DHT 庫進行初始化和讀取
    return 28.5; 
}

// 讀取 HC-SR04 (Placeholder)
int readUltrasound() {
    // 實際：需要使用 pulseIn() 測量 Echo 腳位時間
    return 15; 
}

// 檢查系統狀態 (狀態機的核心邏輯)
void checkSystemState(float light, float temp, int dist) {
    bool anomaly_detected = false;

    // 示例異常判斷邏輯 (請根據真實健康照護需求修改)
    if (dist > 100 && currentState != STATE_CONFIRM_SAFE) { 
        anomaly_detected = true;
    }
    if (temp > 35.0) { 
        anomaly_detected = true;
    }
    
    // --- 狀態機轉移邏輯 ---
    if (anomaly_detected && currentState != STATE_ALERT) {
        currentState = STATE_ALERT;
        Serial.println("!!! ANOMALY DETECTED !!! Transitioning to ALERT state.");
    } 
    else if (!anomaly_detected && currentState == STATE_ALERT) {
        // 異常解除，返回 IDLE
        currentState = STATE_IDLE;
        Serial.println("Anomaly resolved. Returning to IDLE state.");
    }
}

// 執行動作 (Action) - 現只控制蜂鳴器和 OLED 顯示
void actuateSystem() {
    // 1. 蜂鳴器警報控制
    if (currentState == STATE_ALERT) {
        tone(BEEP_PIN, 1000); // 發出 1kHz 警報音
    } else if (currentState != STATE_CONFIRM_SAFE) {
        noTone(BEEP_PIN); // 非警報狀態保持靜音
    }
    
    // 2. 顯示器更新 (取代 LED 狀態指示)
    displayStatus(currentState);
}

// 專門用於顯示系統狀態的函式
void displayStatus(SystemState state) {
    display.clearDisplay(); // 清空前一個畫面
    display.setTextSize(1); // 文字大小
    display.setTextColor(WHITE); // 文字顏色
    
    const char* status_msg;
    if (state == STATE_IDLE) {
        status_msg = "Status: IDLE (OK)";
    } else if (state == STATE_ALERT) {
        status_msg = "!!! WARNING: ALERT !!!"; // 醒目的警報訊息
    } else { // STATE_CONFIRM_SAFE
        status_msg = "Confirm Safe - Press Btn";
    }

    // 顯示核心狀態
    display.setCursor(0, 0);
    display.print(status_msg);
    
    // 顯示採集數據的範例 (為了展示，使用預設值)
    display.setCursor(0, 30);
    display.print("Temp: 28.5C | Dist: 15cm");
    
    // 顯示時間或其他診斷資訊
    display.setCursor(0, 50);
    display.print("System Operational.");

    display.display(); // 將緩衝區內容輸出到 OLED
}
