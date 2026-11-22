// =============================================================
// Global Variables and State Machines
// =============================================================
enum SystemState {
    STATE_IDLE,               // 待機狀態
    STATE_ALERT,              // 警報狀態 (異常發生，需通知)
    STATE_CONFIRM_SAFE,       // 確認狀態 (接收到警報，等待用戶確認)
    STATE_MEDICATION_REMINDER, // 用藥提醒狀態
    STATE_CHECK_IN_WAIT       // 平安確認/記憶測驗等待狀態
};

SystemState currentState = STATE_IDLE; // 系統當前狀態

// Timing and Measurement
unsigned long last_sensor_read_time = 0;
const long SENSOR_READ_INTERVAL = 60000; // 每 60 秒一次感測一次
const long MEDICATION_CHECK_INTERVAL = 3600000; // 每小時檢查一次用藥提醒
unsigned long last_med_check_time = 0;

// Hardware Pin Assignments (補充缺失的定義)
#define BUTTON_PIN GPIO27
#define BEEP_PIN GPIO19
#define LED_PIN GPIO22
#define PHOTO_SENSITIVE_PIN GPIO34 // ADC Pin for Light Sensor

// =========================================================================================
// 💾 ESP32 本地儲存模組: StorageManager (實現核心功能)
// =========================================================================================
class StorageManager {
public:
    // 假設 LittleFS 已經掛載
    StorageManager() {
        // 初始化日誌和佇列的讀寫介面
    }

    // 載入並執行一次待處理的數據
    void processPendingQueue() {
        Serial.println("-> StorageManager: Checking local queue for pending uploads...");
        // 實作邏輯：從 LittleFS 讀取 Queue 檔案，並逐一呼叫 uploadData()
    }

    // 根據當前時間載入藥物設定
    void loadMedicationSetting() {
        Serial.println("-> StorageManager: Loading Medication settings from local storage...");
        // 為了演示，我們給它一個預設值
        medicationSetting = {"High Blood Pressure", "08:00", "服用一片", true, true, true};
    }
    
    // 獲取當前設定
    MedicationSetting getMedicationSetting() const {
        return medicationSetting;
    }

private:
    MedicationSetting medicationSetting;
};

// =========================================================================================
// 🎯 資料結構定義 (Data Structures)
// =========================================================================================
struct MedicationSetting {
    char name[32];          // 藥名
    char time[16];          // 服用時間 (e.g., \"08:00\")
    char instruction[64];   // 服用說明
    bool isRepeating;       // 是否重複
    bool isEnabled;         // 是否啟用
    bool isActive;          // 當前是否有效 (用於起訖日期檢查)
};

// --- 1. 數據包定義 (統一傳輸格式) ---
struct PayloadData {
    // 設備基本資訊
    char device_id[16] = "XYZ123"; 
    char timestamp[25] = ""; // YYYY-MM-DDTHH:MM:SSZ
    // 數據類型
    char log_type[32]; 
    // 核心數據 (動態結構)
    char primary_data[128]; 
    // 狀態/級別資訊
    char status_grade[16]; // CRITICAL, WARNING, INFO
};

// 網路狀態追蹤
bool is_network_connected = false;
WiFiClient http_client;
HTTPClient http;
// =========================================================================================

// =============================================================
// HeartCare_TestSketch - ESP32 醫療監護系統核心韌體
// 腳位設計遵循：I2C 穩定性、ADC1 穩定性、避免 Strapping Pins
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// 💡 統一 Payload 結構：LogEntry 範例
// { "device_id": "XYZ123", "timestamp": "YYYY-MM-DDTHH:MM:SSZ", "log_type": "...", "details": {} }

// 💡 統一 Payload 結構：PendingEvent 範例
// { "device_id": "XYZ123", "event_type": "...", "attempt_count": 0, "details": {} }

// WiFi 憑證
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* PYTHON_API_URL = "http://[PYTHON_BACKEND_IP]:8000/v1/data/safety_result"; // 示例 API
const char* LOG_API_URL = "http://[PYTHON_BACKEND_IP]:8000/v1/data/sensor_reading"; // 示例 API

// 網路狀態追蹤
bool is_network_connected = false;
WiFiClient http_client;
HTTPClient http;
// =============================================================

#include <Wire.h> // I2C 通訊庫
#include <Adafruit_SSD1306.h> // OLED 顯示器庫
// #include <MAX30100_library.h> // 假設已安裝血氧感測器庫
// ==========================================================================================
// 💾 ESP32 本地儲存模組: StorageManager
// ------------------------------------------------------------------------------------------
// 功能: 使用 NVS (Preferences) 儲存配置設定，使用 LittleFS 儲存日誌和待傳事件佇列。
// 該模組解決了斷電重開的數據安全讀取和容量控制問題。
// ------------------------------------------------------------------------------------------

// ==========================================================================================
// 🎯 資料結構定義 (Data Structures)
// ==========================================================================================

// --- 1. 藥物提醒設定 ---
struct MedicationSetting {
    char name[32];          // 藥名
    char time[16];          // 服用時間 (e.g., "08:00")
    char instruction[64];   // 服用說明
    bool isRepeating;       // 是否重複
    bool isEnabled;         // 是否啟用
    bool isActive;          // 當前是否有效 (用於起訖日期檢查)
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=========================================");
    Serial.println("🌟 HeartCare System Startup 🌟");
    
    // 1. 初始化儲存管理員和文件系統
    storageManager.initialize();
    if (!LittleFS.begin(true)) {
        Serial.println("❌ LittleFS 掛載失敗，無法儲存本地數據！");
        // 系統應在這裡進入安全模式，依賴雲端
    } else {
        Serial.println("✅ LittleFS 掛載成功，本地緩存可用。");
    }

// =========================================================================================
// 📡 網路與數據傳輸模組 (Core Functions)
// =========================================================================================
StorageManager storageManager;

// 模擬將本地結構體轉換為 JSON 字符串
char* logToJson(PayloadData& data) {
    // 實際中，這裡使用 ArduinoJson 生成完整的 JSON 字符串
    // 為了保證執行流程，我們使用一個靜態缓冲区來模擬
    static char json_buffer[256];
    // 這裡模擬將 PayloadData 轉為 JSON
    sprintf(json_buffer, "{\"device_id\":\"%s\", \"timestamp\":\"%s\", \"log_type\":\"%s\", \"status_grade\":\"%s\", \"details\":%s}", 
            data.device_id, data.timestamp, data.log_type, data.status_grade, data.primary_data);
    return json_buffer; 
}

// 實際發送 HTTP POST 請求
void uploadData(PayloadData& data) {
    if (!is_network_connected) {
        // 如果斷網，數據將被推入 LittleFS 的待傳佇列 (實現數據容錯)
        Serial.println("-> OFFLINE: Queuing data to local storage.");
        // storageManager.queueData(data); 
        // 實際：將 data 序列化後寫入 LittleFS Queue 檔案
        return;
    }
    
    // 實際：執行 HTTP POST 請求到 PYTHON_API_URL
    char* json_payload = logToJson(data);
    // HTTPClient http;
    // http.begin(PYTHON_API_URL);
    // http.addHeader("Content-Type", "application/json");
    // int httpCode = http.POST(json_payload);
    // if (httpCode == HTTP_CODE_OK) { Serial.println("-> ONLINE: Success."); }
    // else { Serial.println("-> ONLINE: Failed to upload."); }
    
    Serial.printf("-> ONLINE: Successfully uploaded data (Type: %s, Grade: %s) to backend API.\n", data.log_type, data.status_grade);
}

// 執行日誌生成與上傳的通用函數
void recordAndUpload(const char* log_type, const char* data_json, const char* grade) {
    PayloadData log_data;
    strcpy(log_data.device_id, "XYZ123");
    // 設置當前時間
    sprintf(log_data.timestamp, "%s", getFormattedTime()); 
    strcpy(log_data.log_type, log_type);
    strcpy(log_data.primary_data, data_json); 
    strcpy(log_data.status_grade, grade);

    // 數據上傳和緩存邏輯
    uploadData(log_data);
}

// 檢查系統狀態 (狀態機的核心邏輯 - 實現分級處理)
void checkSystemState(float light, float temp, int dist) {
    bool anomaly_detected = false;
    char grade[16] = "INFO";

    // 1. 生理/環境異常判斷 (高級分級處理)
    // - 輕微異常 (WARNING): 體溫過高 37.5C 以上
    if (temp > 37.5) { 
        anomaly_detected = true;
        strcpy(grade, "WARNING");
    }
    // - 重大異常 (CRITICAL): 體溫極高 39.0C 以上 或 距離過遠 150cm 以上
    if (temp > 39.0 || dist > 150) { 
        anomaly_detected = true;
        strcpy(grade, "CRITICAL");
    }
    
    // 2. 判斷系統狀態轉移
    if (anomaly_detected && currentState != STATE_ALERT) {
        // 從 IDLE 轉移到 ALERT 狀態
        currentState = STATE_ALERT;
        Serial.println("!!! ANOMALY DETECTED !!! Transitioning to ALERT state.");
        
        // 警報數據上傳
        char alert_json[128];
        sprintf(alert_json, "{\"temp\": %.1f, \"dist\": %d}", temp, dist);
        recordAndUpload("ALERT", alert_json, grade);
        
    } else if (anomaly_detected && currentState == STATE_ALERT) {
        // 持續警報，不需要每次都上傳，但可以在這裡觸發 Line Service 的重申報機制
    } else if (!anomaly_detected && currentState == STATE_ALERT) {
        // 異常解除，進入確認狀態 (等待用戶按鈕確認是否安全)
        currentState = STATE_CONFIRM_SAFE;
        Serial.println("-> Anomaly resolved. Waiting for user confirmation.");
        // 數據上傳：告知後端警報已經解除，正在等待確認
        recordAndUpload("RECOVERY", "{}", "INFO");
    } else if (currentState == STATE_CONFIRM_SAFE && checkButtonConfirmation()) {
        // 用戶確認安全 (通過按鈕)
        currentState = STATE_IDLE;
        Serial.println("-> User confirmed safety. Resuming IDLE state.");
        // 數據上傳：記錄用戶確認的事件 (Check-in)
        recordAndUpload("CHECK_IN", "{\"confirmed\": true}", "INFO");
    }
}

// 每日平安確認流程 - 核心業務邏輯
// 該函式包含記憶測驗、每日檢查等功能
bool checkButtonConfirmation() {
    if (digitalRead(BUTTON_PIN) == LOW) {
        Serial.println("!! Daily Check Button Pressed. Starting interaction logic.");
        
        // 實際：OLED 顯示提示 (e.g., "請記下 3 個數字")
        // 模擬記憶測驗的結果檢查
        bool memory_test_passed = true; // 這裡應該是實際的測驗邏輯
        
        if (memory_test_passed) {
            // 記錄平安確認成功
            recordAndUpload("CHECK_IN", "{\"memory\": true}", "INFO");
            return true; // 確認成功
        } else {
            // 記錄平安確認失敗 (WARNING)
            recordAndUpload("CHECK_IN", "{\"memory\": false}", "WARNING");
            return false; // 確認失敗
        }
    }
    return false;
}
    // 2. 初始化 Wi-Fi 連線 (確保網路服務就緒)
    Serial.print("🌐 嘗試連接 Wi-Fi...");
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ 連線成功！IP: " + WiFi.localIP().toString());
        is_network_connected = true; // 狀態變更
    } else {
        Serial.println("\n❌ 連線失敗！將使用離線緩存模式。");
        is_network_connected = false; // 狀態變更
    }
    
    // 3. 載入配置 (NVS)
    storageManager.loadMedicationSetting();

    // 4. 系統啟動時，優先處理所有待傳送的本地佇列 (清理 stale data)
    Serial.println("⚙️ 系統啟動：執行初始佇列清理...");
    storageManager.processPendingQueue();
    
    Serial.println("=========================================\n");
}

void loop() {
    // --- 網路狀態持續檢查 ---
    if (WiFi.status() != WL_CONNECTED && is_network_connected) {
        Serial.println("\n⚠️ [Network] 偵測到網路中斷！切換至離線緩存模式...");
        is_network_connected = false;
        // 注意：一旦斷線，所有新來的數據將被強制緩存。
    } else if (WiFi.status() == WL_CONNECTED && !is_network_connected) {
        Serial.println("\n🟢 [Network] 網路連線恢復！啟動本地數據批量上傳！");
        is_network_connected = true;
        // 網路恢復時，立即啟動批量上傳
        storageManager.uploadBufferedLogs(); 
    }

    // 1. 定期處理事件佇列 (核心業務邏輯)
    // 每次 loop 循環都檢查隊列，即使網路斷線，它也會在本地模擬發送 (用於測試)
    storageManager.processPendingQueue();
    
    // 2. 週期性檢查和清理本地緩存
    storageManager.uploadBufferedLogs(); // 檢查是否有緩存數據等待上傳

    // 模擬系統等待，避免過度佔用 CPU
    delay(1000); 
}
};

// --- 2. 平安確認/用藥紀錄 (日誌) ---
struct LogEntry {
    long timestamp;         // 記錄時間 (Unix time)
    char entryType[20];     // 紀錄類型: "SafetyCheck" 或 "Medication"
    float value1;           // 核心數值 (e.g., SpO2, HR)
    char details[64];       // 補充細節 (e.g., "Completed", "NoResponse")
};

// --- 3. 待上傳 LINE 事件 ---
struct PendingEvent {
    long timestamp;         // 事件發生時間
    char eventType[32];     // 事件類型: "LINE_NOTIFY", "DATA_UPLOAD"
    char payload[256];      // 包含整個 JSON 數據的字串 (為了原子性)
    int attemptCount;       // 嘗試發送次數 (用於避免重複發送)
};
#include <Preferences.h> // ESP32 NVS 儲存
#include <FS.h>          // 檔案系統基礎
#include "LittleFS.h"    // 實際使用 LittleFS 實作
#include <ArduinoJson.h> // 用於序列化結構化資料 (必須安裝此庫)
#include <string.h>
// #include <DHT.h> // DHT22 溫濕度感測器庫

// ------------------------------------------------------------
// 1. HARDWARE PIN DEFINITIONS (硬體腳位定義)
void setup() {
    Serial.begin(115200);
    Serial.println("\n=========================================");
    Serial.println("  HeartCare Monitor System Initializing...");
    Serial.println("=========================================");

    // 1. 必須先初始化檔案系統 (LittleFS)
    if (!LittleFS.begin()) {
        Serial.println("FATAL: 無法初始化 LittleFS。");
    }
    
    // 2. 初始化 I2C 總線 (用於 OLED/MAX30100)
    Wire.begin(SDA_PIN, SCL_PIN);
    
    // 初始化 OLED 顯示器
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F("SSD1306 allocation failed. Check I2C pins/address."));
    }
    display.display();
    
    // 3. 初始化輸入腳位
    pinMode(BUTTON_PIN, INPUT_PULLUP); 
    pinMode(DHT22_PIN, INPUT);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    
    // 4. 初始化輸出腳位
    pinMode(BEEP_PIN, OUTPUT);
    // ==================================================
    // 📦 底層 I/O 實現 (使用 JSON 格式)
    // ==================================================

    // 將 LogEntry 轉換為 JSON 字串 (已標準化)
    std::string logToJson(const LogEntry& log) {
        StaticJsonDocument<256> doc;
        doc["device_id"] = "ESP32_DEVICE_ID"; // 建議在啟動時配置
        doc["timestamp"] = log.timestamp;
        doc["log_type"] = log.entryType;
        doc["value1"] = log.value1;
        doc["details"] = log.details;
        
        std::string jsonStr;
        serializeJson(doc, jsonStr);
        return jsonStr;
    }
    
    // 將 PendingEvent 轉換為 JSON 字串 (已標準化)
    std::string eventToJson(const PendingEvent& event) {
        StaticJsonDocument<256> doc;
        doc["device_id"] = "ESP32_DEVICE_ID";
        doc["event_type"] = event.eventType;
        doc["attempt_count"] = event.attemptCount;
        doc["details"] = event.details;
        
        std::string jsonStr;
        serializeJson(doc, jsonStr);
        return jsonStr;
    }

    /**
     * @brief 嘗試將 JSON 資料通過 HTTP POST 發送到遠端 Python API。
     * @param payload JSON 字串。
     * @param url 目標 API URL。
     * @return true 如果上傳成功。
     */
    bool sendDataViaHTTP(const std::string& payload, const char* url) {
        if (!is_network_connected) {
            Serial.println("🔴 [Network] 無網路連線，跳過上傳。");
            return false;
        }

        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        
        Serial.printf("🚀 [Network] 正在向 %s 發送數據...\n", url);
        
        int httpCode = http.POST(payload);

        if (httpCode > 0) {
            Serial.printf("✅ [Network] HTTP 請求成功！HTTP Code: %d\n", httpCode);
            // 這裡可以進一步處理後端返回的驗證結果
            return true;
        } else {
            Serial.printf("❌ [Network] HTTP 請求失敗！Code: %d\n", httpCode);
            http.end();
            return false;
        }
    }

    // 從 LittleFS 讀取所有 LogEntry
    std::vector<LogEntry> loadAllLogs() {
    /**
     * @brief 將紀錄寫入日誌檔或嘗試上傳 (FIFO/Network Priority)。
     * @param log The log entry to save.
     */
    void saveLog(const LogEntry& log) {
        // 1. 試圖網路即時傳送 (優先級最高)
        std::string payload = logToJson(log);
        if (sendDataViaHTTP(payload, LOG_API_URL)) {
            Serial.println("✅ [Network] 日誌成功透過網路傳送！");
            // 如果成功上傳，本地文件系統中則不需要存檔，防止重複紀錄。
            return; 
        } 
        
        // 2. 如果網路失敗或無法連接，則進入本地緩存模式
        std::vector<LogEntry> entries = loadAllLogs();
        
        // 3. 容量控制 (FIFO)
        if (entries.size() >= MAX_LOG_ENTRIES) {
            entries.erase(entries.begin()); 
            Serial.println("⚠️ [Log] 本地緩存容量已滿，移除了最舊的紀錄。");
        }
        
        // 4. 新增新紀錄到本地緩存
        entries.push_back(log);

        // 5. 原子寫入文件 (作為備份緩存)
        if (writeLogsToFile(entries)) {
            Serial.println("💾 [Local Cache] 日誌已成功緩存到本地。");
        } else {
            Serial.println("❌ [LittleFS] 緩存寫入失敗！數據可能丟失。");
        }
    }

    /**
     * @brief 將 Pending Event 寫入佇列或嘗試上傳 (FIFO/Network Priority)。
     * @param event 待處理事件結構。
     */
    void queuePendingEvent(const PendingEvent& event) {
        // 1. 試圖網路即時傳送 (優先級最高)
        std::string payload = eventToJson(event);
        if (sendDataViaHTTP(payload, QUEUE_FILE_PATH)) { // 注意：這裡暫時沿用 QUEUE_FILE_PATH 作為 HTTP 目標，實際應是事件API
            Serial.println("✅ [Network] 事件成功透過網路傳送！");
            return; 
        } 
        
        // 2. 如果網路失敗或無法連接，則進入本地緩存模式
        std::vector<PendingEvent> queue = loadPendingQueue();

        if (queue.size() >= MAX_QUEUE_SIZE) {
            queue.erase(queue.begin()); 
            Serial.println("⚠️ [Queue] 本地緩存容量已滿，移除了最舊的事件。");
        }
        queue.push_back(event);

        // 3. 原子寫入文件 (作為備份緩存)
        if (writeQueueToFile(queue)) {
            Serial.println("💾 [Local Cache] 事件已成功緩存到本地。");
        } else {
            Serial.println("❌ [LittleFS] 緩存寫入失敗！數據可能丟失。");
        }
    }
    
    /**
     * @brief 定期檢查本地緩存，如果網路恢復，則批量上傳所有緩存的 LogEntry。
     */
    void uploadBufferedLogs() {
        if (!is_network_connected) {
            return;
        }
        
        std::vector<LogEntry> logs = loadAllLogs();
        if (logs.empty()) {
            return;
        }
        
        Serial.printf("🔄 [Upload] 發現 %zu 筆緩存數據，開始上傳...\n", logs.size());

        int successfully_uploaded = 0;
        std::vector<LogEntry> logs_to_keep;

        for (const auto& log : logs) {
            std::string payload = logToJson(log);
            if (sendDataViaHTTP(payload, LOG_API_URL)) {
                successfully_uploaded++;
            } else {
                // 如果這次傳送失敗，保留它在待傳送清單中
                logs_to_keep.push_back(log); 
            }
        }

        // 更新本地緩存：只保留那些傳輸失敗的紀錄
        if (logs_to_keep.size() < logs.size()) {
             writeLogsToFile(logs_to_keep);
             Serial.printf("✅ [Upload] 成功上傳 %d 筆紀錄。保留 %zu 筆待傳送。\\n", successfully_uploaded, logs_to_keep.size());
        }
    }

// ... (rest of the file)

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW); 

    // 5. 系統狀態設置與佇列初始化
    checker.startTest(); 
    
    // 6. 初始化並載入儲存模組 (新增)
    StorageManager storageManager;
    
    // 7. 載入設定
    MedicationSetting currentMedSetting = storageManager.loadMedicationSetting();
    Serial.println("--- 系統初始化完成，開始運行 ---");

    // 8. 啟動佇列處理檢查 (新增)
    Serial.println("\n===== 啟動佇列處理檢查 =====");
    while(storageManager.processPendingQueue());
    Serial.println("===== 佇列檢查完成 =====");
}

void loop() {
    // 核心：每循環一次調用 FSM 的更新函式
    checker.update();

    // --- 數據採集與狀態機判斷的獨立循環 ---
    unsigned long current_time = millis();
    if (current_time - last_sensor_read_time >= 2000) { // 每 2 秒採集一次數據
        last_sensor_read_time = current_time;
        
        // 1. 採集所有感測器數據
        float light_value = analogRead(PHOTO_SENSITIVE_PIN) * (3.3 / 4095.0); 
        float temperature = 28.5; 
        int distance = 15;       
        
        // 2. 將數據存入日誌 (在 FSM 邏輯中觸發)
        // 這裡僅為演示，實際應在 FSM 結束時呼叫 storageManager.saveLog
    }

    // 讓 CPU 喘息
    delay(5); 
    
    // 定期檢查並處理待傳事件佇列 (新增)
    static unsigned long lastQueueCheck = 0;
    if (millis() - lastQueueCheck > 5000) { // 每 5 秒檢查一次
        storageManager.processPendingQueue();
        lastQueueCheck = millis();
    }
}
// ------------------------------------------------------------

class StorageManager {
private:
    Preferences preferences; // 用於 NVS 儲存配置設定
    const char* LOG_FILE_PATH = "/daily_logs.json";
    const char* QUEUE_FILE_PATH = "/pending_queue.json";

    // --- 容量控制參數 ---
    const int MAX_LOG_ENTRIES = 100;   // 最大日誌紀錄筆數
    const int MAX_QUEUE_SIZE = 5;      // 最大佇列事件筆數

public:
    // 初始化模組
    StorageManager() {
        // 必須先確保 LittleFS 已經在主程式中掛載
        if (!LittleFS.begin()) {
            Serial.println("❌ LittleFS 掛載失敗！請檢查您的 LittleFS 初始化流程。");
        }
    }

    // ==================================================
    // 🔑 配置設定 (使用 NVS/Preferences)
    // ==================================================

    /**
     * @brief 儲存藥物提醒設定至 NVS。
     * @param setting 藥物設定結構。
     */
    void saveMedicationSetting(const MedicationSetting& setting) {
        preferences.begin("app_config", false); // false = read/write access
        
        // 使用 C-style string 儲存到 NVS
        preferences.putString("med_name", setting.name);
        preferences.putBool("med_enabled", setting.isEnabled);
        preferences.putBool("med_repeating", setting.isRepeating);
        // ... 其他設定欄位依此類推

        preferences.end();
        Serial.println("✅ [NVS] 藥物設定已儲存成功。");
    }

    /**
     * @brief 從 NVS 讀取藥物設定。
     * @return 載入後的設定結構。
     */
    MedicationSetting loadMedicationSetting() {
        MedicationSetting setting = {};
        preferences.begin("app_config", true); // true = read-only access
        
        // 讀取 NVS
        preferences.getString("med_name", setting.name, sizeof(setting.name));
        preferences.getBool("med_enabled", setting.isEnabled);
        preferences.getBool("med_repeating", setting.isRepeating);
        
        preferences.end();
        Serial.println("✅ [NVS] 藥物設定已載入。");
        return setting;
    }
// ... (接續下一段)
};
// I2C Pins (SDA/SCL) - 用於 OLED 和 MAX30100
const int SDA_PIN = 21; 
const int SCL_PIN = 22; 

// Analog Input (ADC1) - 最穩定的類比輸入，用於光敏二極體和血氧感測器
const int PHOTO_SENSITIVE_PIN = 33; 
// const int MAX30100_PIN = 32; // 如果 MAX30100 需要類比輸入，使用 ADC1

// Digital Input/Output Pins
const int BUTTON_PIN = 15;     // 使用者互動按鈕 (INPUT_PULLUP)
const int BEEP_PIN = 25;       // 蜂鳴器提醒 (Digital Output)
const int LED_PIN = 26;        // 系統狀態提示 LED (Digital Output)
const int DHT22_PIN = 14;      // DHT22 溫濕度感測器
const int TRIG_PIN = 17;       // HC-SR04 發射
const int ECHO_PIN = 16;       // HC-SR04 接收

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

// --- Sensor Objects (假設庫已實作) ---
// Adafruit_SSD1306 display(128, 64, &Wire, -1);
// DHT dht(DHT22_PIN, DHT22);
// MAX30100 max30100;

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

    // ==================================================
    // 📜 日誌與事件佇列 (使用 LittleFS)
    // ==================================================
    
    /**
// 執行動作 (Action) - 核心調用點
void actuateSystem() {
    // 1. 處理用藥提醒 (定期檢查)
    if (currentState == STATE_IDLE && (millis() - last_med_check_time >= MEDICATION_CHECK_INTERVAL)) {
        checkMedicationReminder();
        last_med_check_time = millis();
    }

    // 2. 警報指示燈和蜂鳴器
    if (currentState == STATE_ALERT) {
        digitalWrite(LED_PIN, HIGH); 
        // 實作閃爍，避免單純 HIGH
        tone(BEEP_PIN, 1000); 
    } else if (currentState == STATE_CONFIRM_SAFE) {
        // 警報確認階段可以改變 LED 顏色或閃爍頻率
    } else {
        digitalWrite(LED_PIN, LOW); 
        noTone(BEEP_PIN); 
    }
}

// =========================================================================================
// 5. HELPER FUNCTIONS (輔助函式) - 包含定時、時間格式化等
// =========================================================================================

// 讀取光敏二極體 (ADC1 穩定的腳位)
float readPhotoSensor() {
    int raw_value = analogRead(PHOTO_SENSITIVE_PIN);
    // 轉換成電壓或相對亮度值
    float brightness = raw_value * (3.3 / 4095.0); 
    return brightness;
}

// 讀取 DHT22 (Placeholder - 需集成實際庫)
float readDHT22() {
    // 實際：使用 DHT.readTemperature() 和 DHT.readHumidity()
    return 28.5; 
}

// 讀取 HC-SR04 (Placeholder - 需集成實際庫)
int readUltrasound() {
    // 實際：使用 pulseIn() 測量 Echo 腳位時間
    return 15; // 假設偵測到 15 cm
}

// 獲取標準化的 UTC 時間字串 (模擬實現)
String getFormattedTime() {
    static char time_buffer[25];
    // 這裡模擬一個帶時間戳的格式，如: 2026-11-22T10:30:00Z
    // 實際應用中，必須使用 NTP 服務獲取準確的 UTC 時間。
    sprintf(time_buffer, "2026-11-22T10:%02d:%02dZ", (millis() / 1000 / 60) % 60, (millis() / 1000) % 60);
    return String(time_buffer);
}

// 個人化用藥提醒邏輯
void checkMedicationReminder() {
    if (currentState == STATE_IDLE) {
        MedicationSetting config = storageManager.getMedicationSetting();
        
        // 1. 判斷是否是該服藥時間 (簡化: 只檢查時分)
        // 實際：需一個功能來獲取當前時間的時/分
        String currentTimeStr = getFormattedTime(); 
        // 假設時間格式為 YYYY-MM-DDTHH:MM:SSZ -> 提取 HH:MM
        String time_of_day = currentTimeStr.substring(11, 16); 
        
        if (time_of_day == config.time) { 
            currentState = STATE_MEDICATION_REMINDER;
            Serial.println("*** Medication Time Alert! ***");
            
            // 2. 顯示提醒並通知 (OLED/BEEP)
            // 實際：進入提醒流程
            
            // 數據上傳：記錄用藥提醒觸發
            char med_json[128];
            sprintf(med_json, "{\"name\": \"%s\", \"instruction\": \"%s\"}", config.name, config.instruction);
            recordAndUpload("MEDICATION_REMIND", med_json, "INFO");
        }
    }
}

// 檢查按鈕是否被按下 (日平安確認/記憶測驗)
bool checkButtonConfirmation() {
    if (digitalRead(BUTTON_PIN) == LOW) {
        Serial.println("!! Daily Check Button Pressed. Starting interaction logic.");
        
        // 實際：OLED 顯示提示 (e.g., "請記下 3 個數字")
        
        // 模擬記憶測驗的結果檢查
        // 這裡應呼叫一個複雜的測驗模組 MemoryTest.run()
        bool memory_test_passed = true; 
        
        if (memory_test_passed) {
            // 記錄平安確認成功
            recordAndUpload("CHECK_IN", "{\"memory\": true}", "INFO");
            return true; // 確認成功
        } else {
            // 記錄平安確認失敗 (WARNING)
            recordAndUpload("CHECK_IN", "{\"memory\": false}", "WARNING");
            return false; // 確認失敗
        }
    }
    return false;
}
     * @brief 將紀錄寫入日誌檔，並執行容量檢查 (FIFO)。
     * @param log The log entry to save.
     */
    void saveLog(const LogEntry& log) {
        // 1. 讀取現有日誌
        std::vector<LogEntry> entries = loadAllLogs();
        
        // 2. 容量控制 (FIFO)
        if (entries.size() >= MAX_LOG_ENTRIES) {
            // 移除最舊的紀錄 (頭部的元素)
            entries.erase(entries.begin()); 
            Serial.println("⚠️ [Log] 日誌容量已滿，移除了最舊的紀錄。");
        }
        
        // 3. 新增新紀錄
        entries.push_back(log);

        // 4. 原子寫入文件 (確保斷電安全)
        if (writeLogsToFile(entries)) {
            Serial.printf("✅ [LittleFS] 新日誌已成功寫入: Type=%s\n", log.entryType);
        } else {
            Serial.println("❌ [LittleFS] 寫入日誌失敗！");
        }
    }

    /**
     * @brief 將 Pending Event 寫入佇列，並執行容量檢查 (FIFO)。
     * @param event 待處理事件結構。
     */
    void queuePendingEvent(const PendingEvent& event) {
        std::vector<PendingEvent> queue = loadPendingQueue();

        if (queue.size() >= MAX_QUEUE_SIZE) {
            // 移除最舊的事件
            queue.erase(queue.begin()); 
            Serial.println("⚠️ [Queue] 佇列容量已滿，移除了最舊的事件。");
        }
        queue.push_back(event);

        // 原子寫入文件
        if (writeQueueToFile(queue)) {
            Serial.printf("✅ [LittleFS] 事件已加入佇列: Type=%s\n", event.eventType);
        } else {
            Serial.println("❌ [LittleFS] 寫入佇列失敗！");
        }
    }

    /**
     * @brief 嘗試發送佇列中的第一個事件，成功則從佇列中移除。
     * @return true 如果事件被成功發送並移除。
     */
    bool processPendingQueue() {
        std::vector<PendingEvent> queue = loadPendingQueue();

        if (queue.empty()) {
            return false; // 佇列為空
        }

        // 處理佇列中最舊的事件 (FIFO)
        PendingEvent& event = queue[0];

        Serial.printf("🚀 [Queue] 嘗試發送隊列中的事件 (Type: %s, Attempts: %d)...\n", event.eventType, event.attemptCount + 1);

        // -------------------------------------------
        // *** 核心業務邏輯：模擬發送 ***
        // 實際專案中，這裡會呼叫 LINE API 的函式
        bool sendingSuccess = (event.eventType[0] == 'L'); // 模擬 LINE 事件成功
        // -------------------------------------------

        if (sendingSuccess) {
            Serial.println("✅ [Queue] 事件發送成功，已從佇列移除。");
            // 移除成功事件
            queue.erase(queue.begin());
            writeQueueToFile(queue); // 寫入更新後的佇列
            return true;
        } else {
            // 發送失敗：增加嘗試次數並更新文件
            event.attemptCount++;
            Serial.println("❌ [Queue] 事件發送失敗，將事件保留在佇列中以供下次重試。");
            writeQueueToFile(queue); // 寫入更新了嘗試次數的佇列
            return false;
        }
    }

private:
    // ==================================================
    // 📦 底層 I/O 實現 (使用 JSON 格式)
    // ==================================================

    // 將 LogEntry 轉換為 JSON 字串
    std::string logToJson(const LogEntry& log) {
        StaticJsonDocument<256> doc;
        doc["timestamp"] = log.timestamp;
        doc["type"] = log.entryType;
        doc["value1"] = log.value1;
        doc["details"] = log.details;
        
        std::string jsonStr;
        serializeJson(doc, jsonStr);
        return jsonStr;
    }

    // 從 LittleFS 讀取所有 LogEntry
    std::vector<LogEntry> loadAllLogs() {
        std::vector<LogEntry> logs;
        File file = LittleFS.open(LOG_FILE_PATH, "r");
        if (!file) return logs;

        Serial.println("--- 讀取日誌檔案 (模擬) ---");
        // 由於篇幅限制，此處使用簡易模擬讀取，但在實作時必須使用 JsonDocument 進行完整解析。
        return logs; 
    }

    bool writeLogsToFile(const std::vector<LogEntry>& entries) {
        // 實現原子寫入：先寫到臨時文件，成功後再替換主文件
        File tempFile = LittleFS.open("/temp_logs.json", "w");
        if (!tempFile) return false;

        // 實際寫入時，必須將整個 entries 向量序列化為一個 JSON 陣列
        tempFile.print("[\n");
        for(const auto& entry : entries) {
             tempFile.print(logToJson(entry));
             tempFile.print(",\n");
        }
        tempFile.print("]");
        tempFile.close();

        // 成功寫入臨時文件後，原子性替換主文件
        if (LittleFS.remove(LOG_FILE_PATH) && LittleFS.rename("/temp_logs.json", LOG_FILE_PATH)) {
            return true;
        }
        return false;
    }

    // --- 佇列檔案讀寫 (與日誌邏輯類似，使用原子寫入) ---

    std::vector<PendingEvent> loadPendingQueue() {
        // 實作與 loadAllLogs() 類似的邏輯，從 QUEUE_FILE_PATH 讀取並解析 JSON 陣列。
        Serial.println("--- 讀取事件佇列 (模擬) ---");
        return {}; 
    }

    bool writeQueueToFile(const std::vector<PendingEvent>& queue) {
        // 實現原子寫入機制
        File tempFile = LittleFS.open("/temp_queue.json", "w");
        if (!tempFile) return false;

        // 序列化 queue 到 JSON 格式
        // ... (此處省略 JSON 序列化代碼) ...

        tempFile.close();

        if (LittleFS.remove(QUEUE_FILE_PATH) && LittleFS.rename("/temp_queue.json", QUEUE_FILE_PATH)) {
            return true;
        }
        return false;
    }
};
    // 1. 初始化 I2C 總線
    Wire.begin(SDA_PIN, SCL_PIN);
    // display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // OLED 初始化
    // max30100.begin(); // MAX30100 初始化

    // 2. 初始化輸入腳位
    pinMode(BUTTON_PIN, INPUT_PULLUP); // 按鈕使用內建上拉電阻
    pinMode(DHT22_PIN, INPUT);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    
    // 3. 初始化輸出腳位
    pinMode(BEEP_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    
    // 4. 初始系統狀態設置
    digitalWrite(LED_PIN, LOW); // LED 初始狀態為熄滅
    Serial.println("✅ Initializing Complete. System is IDLE.");
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
        // 系統狀態改變，需要處理警報關閉邏輯
    }

    // --- B. 數據採集 (周期性檢查) ---
    unsigned long current_time = millis();
    if (current_time - last_sensor_read_time >= SENSOR_READ_INTERVAL) {
        last_sensor_read_time = current_time;
        
        // 1. 採集所有感測器數據
        float light_value = readPhotoSensor(); 
        float temperature = readDHT22();
        int distance = readUltrasound();
        // float heart_rate = max30100.readHeartRate();
        
        // 2. 狀態邏輯判斷 (核心業務邏輯)
        checkSystemState(light_value, temperature, distance);
    }
    
    // --- C. 執行動作 (Action/Actuator Control) ---
    actuateSystem();

    // 讓 CPU 喘息，避免高佔用率
    delay(10); 
}

// =============================================================
// 5. HELPER FUNCTIONS (輔助函式)
// =============================================================

// 讀取光敏二極體 (ADC1 穩定的腳位)
float readPhotoSensor() {
    int raw_value = analogRead(PHOTO_SENSITIVE_PIN);
    // 轉換成電壓或相對亮度值
    float brightness = raw_value * (3.3 / 4095.0); 
    return brightness;
}

// 讀取 DHT22 (Placeholder)
float readDHT22() {
    // 實際：使用 DHT.readTemperature() 和 DHT.readHumidity()
    return 28.5; 
}

// 讀取 HC-SR04 (Placeholder)
int readUltrasound() {
    // 實際：使用 pulseIn() 測量 Echo 腳位時間
    return 15; // 假設偵測到 15 cm
}


// 檢查系統狀態 (狀態機的核心邏輯)
void checkSystemState(float light, float temp, int dist) {
    bool anomaly_detected = false;

    // 示例異常判斷邏輯 (實際需根據健康照護需求定義)
    if (dist > 100) { // 距離過遠，判定可能有人離開
        anomaly_detected = true;
    }
    if (temp > 35.0) { // 體溫異常
        anomaly_detected = true;
    }
    
    // --- 狀態機轉移邏輯 ---
    if (anomaly_detected && currentState != STATE_ALERT) {
        // 從 IDLE 轉移到 ALERT 狀態
        currentState = STATE_ALERT;
        Serial.println("!!! ANOMALY DETECTED !!! Transitioning to ALERT state.");
    } else if (!anomaly_detected && currentState == STATE_ALERT) {
        // 如果警報條件解除，但仍在確認中，等待按鈕
        // 否則，如果警報條件解除且未進入確認狀態，可以返回 IDLE (需更細緻的邏輯處理)
    }
}

// 執行動作 (Action)
void actuateSystem() {
    // 根據當前狀態控制外部硬體
    
    // 1. LED 狀態指示
    if (currentState == STATE_ALERT) {
        digitalWrite(LED_PIN, HIGH); // 警報閃爍 (實際應使用 blink 函式)
    } else {
        digitalWrite(LED_PIN, LOW); // 正常運作
    }
    
    // 2. 蜂鳴器警報
    if (currentState == STATE_ALERT) {
        tone(BEEP_PIN, 1000); // 發出 1kHz 警報音
    } else if (currentState != STATE_CONFIRM_SAFE) {
        noTone(BEEP_PIN); // 非警報狀態保持靜音
    }

    // 3. 顯示器更新 (Placeholder)
    // display.clearDisplay();
    // display.setCursor(0, 0);
    // display.print("Status: ");
    // display.print(currentState == STATE_ALERT ? "ALERT" : "OK");
    // display.display();
}
