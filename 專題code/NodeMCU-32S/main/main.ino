// ======================================================================
// INTEGRATED MAIN SKETCH - HeartCare IoT Backend
// 合併: HeartCare_TestSketch.ino, SafetyCheckFSM.ino, StorageManager.ino
// 單一檔案版本
// ----------------------------------------------------------------------
// WARNING: This is a monolithic file for easy compilation. 
// For long-term maintenance, please adopt a proper .h/.cpp module structure (Method 2).
// ======================================================================

// --- GLOBAL LIBRARIES ---
#include <Wire.h> 
#include <Adafruit_SSD1306.h> 
#include <MAX30100_PulseOximeter.h> 
#include <DHT.h> 
#include <Preferences.h> 
#include <FS.h>          
#include <LittleFS.h>    
#include <ArduinoJson.h> 
#include <WiFi.h>
#include <vector>
#include <chrono>
#include <ctime>
#include <cmath>
#include <string.h>

// =========================================================================================
// CORE SYSTEM DEFINITIONS (從 HeartCare_TestSketch.ino 提取)
// =========================================================================================

// --- Sensor/System State Definitions ---
enum SystemState {
    STATE_IDLE,          // 正常監測狀態
    STATE_ALERT,         // 異常警報狀態
    STATE_CONFIRM_SAFE   // 平安確認狀態
};
SystemState currentState = STATE_IDLE;

// --- FSM Definition (從 SafetyCheckFSM.ino 提取) ---
enum State {
    STANDBY,              // 待機狀態：等待開始訊號
    ALERTING,             // 提醒中：觸發警報
    PATTERN_DISPLAY,      // 圖案序列展示：展示記憶圖案
    WAITING_INPUT,        // 按鈕連擊計次與停頓結算：等待使用者記憶後按鈕
    VITAL_MEASUREMENT,    // 生理量測：執行感測器讀數
    COMPLETED,            // 已完成
    MEASUREMENT_FAILED,   // 量測失敗：感測器讀值異常
    NO_RESPONSE           // 尚未回應：狀態超時，未完成互動
};

// --- HARDWARE PIN DEFINITIONS (從 HeartCare_TestSketch.ino 提取) ---
const int OLED_SDA_PIN = 21; 
const int OLED_SCL_PIN = 22; 
const int MAX30100_SDA_PIN = 19; 
const int MAX30100_SCL_PIN = 18; 
const int DHT22_PIN = 14;      
const int PHOTO_SENSITIVE_PIN = 34; 
const int HCSR04_TRIG_PIN = 16;  
const int HCSR04_ECHO_PIN = 17;  
const int BUTTON_PIN = 27;      
const int BEEP_PIN = 25;        

// --- Sensor/Display Objects ---
Adafruit_SSD1306 display(128, 64, &Wire, -1); 
DHT dht(DHT22_PIN, DHT22);
PulseOximeter pox;
bool poxReady = false;

// --- Sensor Latest Readings ---
float latestTempC = NAN;
float latestHumidity = NAN;
int latestLightRaw = 0;
long latestDistanceCm = -1;
float latestHeartRate = 0.0f;
float latestSpO2 = 0.0f;

// --- Time Management ---
unsigned long last_sensor_read_time = 0;
const long SENSOR_READ_INTERVAL = 2000;
unsigned long last_display_update_time = 0;
const long DISPLAY_UPDATE_INTERVAL = 500; // OLED 每 500ms 更新一次

// 超聲波測距輔助函式（若未接 HC-SR04 或逾時回傳 -1）
long readUltrasoundCm() {
    digitalWrite(HCSR04_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(HCSR04_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(HCSR04_TRIG_PIN, LOW);

    long duration = pulseIn(HCSR04_ECHO_PIN, HIGH, 30000);
    if (duration <= 0) {
        return -1;
    }
    return duration * 0.0343 / 2;
}

// 讀取 MAX30100 真實心率與血氧數值
bool readMax30100Vitals(float& hrOut, float& spo2Out) {
    if (!poxReady) {
        return false;
    }
    pox.update();
    float hr = pox.getHeartRate();
    float spo2 = pox.getSpO2();
    if (hr > 0.0f || spo2 > 0.0f) {
        hrOut = hr;
        spo2Out = spo2;
        return true;
    }
    return false;
}

// =========================================================================================
// STORAGE MANAGER CLASS (從 StorageManager.ino 提取)
// 實作了 NVS (Preferences) 和 LittleFS (日誌/佇列) 的功能
// =========================================================================================
struct MedicationSetting {
    char name[32];
    char time[16];
    char instruction[64];
    bool isRepeating;
    bool isEnabled;
    bool isActive;
};

struct LogEntry {
    long timestamp;
    char entryType[20];
    float value1;
    char details[64];
};

struct PendingEvent {
    long timestamp;
    char eventType[32];
    char payload[256];
    int attemptCount;
};

class StorageManager {
private:
    Preferences preferences; 
    const char* LOG_FILE_PATH = "/daily_logs.json";
    const char* QUEUE_FILE_PATH = "/pending_queue.json";

    const int MAX_LOG_ENTRIES = 100;   
    const int MAX_QUEUE_SIZE = 5;      

    // 輔助函式: JSON 轉換
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

    // 輔助函式: 讀取日誌檔案 (LittleFS + ArduinoJson)
    std::vector<LogEntry> loadAllLogs() {
        std::vector<LogEntry> logs;
        if (!LittleFS.exists(LOG_FILE_PATH)) {
            return logs;
        }

        File file = LittleFS.open(LOG_FILE_PATH, "r");
        if (!file) {
            Serial.println("[LittleFS] 無法開啟日誌檔案讀取！");
            return logs;
        }

        DynamicJsonDocument doc(8192);
        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error) {
            Serial.printf("[LittleFS] 日誌 JSON 解析失敗: %s\n", error.c_str());
            return logs;
        }

        if (!doc.is<JsonArray>()) {
            return logs;
        }

        for (JsonObject obj : doc.as<JsonArray>()) {
            LogEntry entry = {};
            entry.timestamp = obj["timestamp"] | 0L;
            strlcpy(entry.entryType, obj["type"] | "", sizeof(entry.entryType));
            entry.value1 = obj["value1"] | 0.0f;
            strlcpy(entry.details, obj["details"] | "", sizeof(entry.details));
            logs.push_back(entry);
        }

        return logs;
    }

    // 輔助函式: 寫入日誌 (原子寫入)
    bool writeLogsToFile(const std::vector<LogEntry>& entries) {
        File tempFile = LittleFS.open("/temp_logs.json", "w");
        if (!tempFile) return false;

        tempFile.print("[\n");
        for (size_t i = 0; i < entries.size(); ++i) {
            tempFile.print(logToJson(entries[i]).c_str());
            if (i + 1 < entries.size()) {
                tempFile.print(",\n");
            } else {
                tempFile.print("\n");
            }
        }
        tempFile.print("]\n");
        tempFile.close();

        if (LittleFS.exists(LOG_FILE_PATH)) {
            LittleFS.remove(LOG_FILE_PATH);
        }
        if (LittleFS.rename("/temp_logs.json", LOG_FILE_PATH)) {
            Serial.printf("[LittleFS] 新日誌已成功寫入: Entry Count = %d\n", entries.size());
            return true;
        }
        Serial.println("[LittleFS] 寫入日誌失敗！");
        return false;
    }

    // 輔助函式: 讀取事件佇列檔案 (LittleFS + ArduinoJson)
    std::vector<PendingEvent> loadPendingQueue() {
        std::vector<PendingEvent> queue;
        if (!LittleFS.exists(QUEUE_FILE_PATH)) {
            return queue;
        }

        File file = LittleFS.open(QUEUE_FILE_PATH, "r");
        if (!file) {
            Serial.println("[LittleFS] 無法開啟事件佇列檔案讀取！");
            return queue;
        }

        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error) {
            Serial.printf("[LittleFS] 事件佇列 JSON 解析失敗: %s\n", error.c_str());
            return queue;
        }

        if (!doc.is<JsonArray>()) {
            return queue;
        }

        for (JsonObject obj : doc.as<JsonArray>()) {
            PendingEvent ev = {};
            ev.timestamp = obj["timestamp"] | 0L;
            strlcpy(ev.eventType, obj["eventType"] | "", sizeof(ev.eventType));
            strlcpy(ev.payload, obj["payload"] | "", sizeof(ev.payload));
            ev.attemptCount = obj["attemptCount"] | 0;
            queue.push_back(ev);
        }

        return queue;
    }

    // 輔助函式: 寫入事件佇列 (原子寫入)
    bool writeQueueToFile(const std::vector<PendingEvent>& queue) {
        File tempFile = LittleFS.open("/temp_queue.json", "w");
        if (!tempFile) return false;

        DynamicJsonDocument doc(4096);
        JsonArray arr = doc.to<JsonArray>();
        for (const auto& ev : queue) {
            JsonObject obj = arr.createNestedObject();
            obj["timestamp"] = ev.timestamp;
            obj["eventType"] = ev.eventType;
            obj["payload"] = ev.payload;
            obj["attemptCount"] = ev.attemptCount;
        }

        serializeJson(doc, tempFile);
        tempFile.close();

        if (LittleFS.exists(QUEUE_FILE_PATH)) {
            LittleFS.remove(QUEUE_FILE_PATH);
        }
        if (LittleFS.rename("/temp_queue.json", QUEUE_FILE_PATH)) {
            return true;
        }
        Serial.println("[LittleFS] 寫入事件佇列失敗！");
        return false;
    }


public:
    StorageManager() {}

    // 初始化儲存模組 (需在 LittleFS.begin() 之後呼叫)
    void begin() {
        if (!LittleFS.begin(true)) {
            Serial.println("FATAL: LittleFS 初始化與格式化皆失敗！");
        } else {
            Serial.println("[StorageManager] LittleFS 初始化成功。");
        }
    }

    // --- 配置設定 (NVS) ---
    void saveMedicationSetting(const MedicationSetting& setting) {
        preferences.begin("app_config", false); 
        preferences.putString("med_name", setting.name);
        preferences.putBool("med_enabled", setting.isEnabled);
        preferences.putBool("med_repeating", setting.isRepeating);
        preferences.end();
        Serial.println("[NVS] 藥物設定已儲存成功。");
    }

    MedicationSetting loadMedicationSetting() {
        MedicationSetting setting = {};
        preferences.begin("app_config", true); 
        preferences.getString("med_name", setting.name, sizeof(setting.name));
        preferences.getBool("med_enabled", setting.isEnabled);
        preferences.getBool("med_repeating", setting.isRepeating);
        preferences.end();
        Serial.println("[NVS] 藥物設定已載入。");
        return setting;
    }

    // --- 日誌與事件佇列 (LittleFS) ---
    void saveLog(const LogEntry& log) {
        std::vector<LogEntry> entries = loadAllLogs();
        
        if (entries.size() >= MAX_LOG_ENTRIES) {
            entries.erase(entries.begin()); 
            Serial.println("⚠️ [Log] 日誌容量已滿，移除了最舊的紀錄。");
        }
        entries.push_back(log);

        writeLogsToFile(entries);
    }

    void queuePendingEvent(const PendingEvent& event) {
        std::vector<PendingEvent> queue = loadPendingQueue();
        
        if (queue.size() >= MAX_QUEUE_SIZE) {
            queue.erase(queue.begin()); 
            Serial.println("[Queue] 佇列容量已滿，移除了最舊的事件。");
        }
        queue.push_back(event);

        if (writeQueueToFile(queue)) {
            Serial.printf("[LittleFS] 事件已加入佇列: Type=%s\n", event.eventType);
        }
    }

    bool processPendingQueue() {
        std::vector<PendingEvent> queue = loadPendingQueue();
        if (queue.empty()) return false;

        PendingEvent& event = queue[0];
        Serial.printf("[Queue] 檢查佇列事件 (Type: %s, Attempts: %d)...\n", event.eventType, event.attemptCount + 1);

        // 真實網路狀態檢查：無 Wi-Fi 連線時不假裝送出成功
        if (WiFi.status() != WL_CONNECTED) {
            event.attemptCount++;
            Serial.println("[Queue] Wi-Fi 未連線，事件保留在佇列中等待重試。");
            writeQueueToFile(queue);
            return false;
        }

        // 已連線時可在此串接真實 HTTPClient / LINE Webhook 發送；
        // 尚未設定後端端點前，保留事件並記錄重試次數
        event.attemptCount++;
        Serial.println("[Queue] 尚未設定後端傳送端點，事件保留在佇列中。");
        writeQueueToFile(queue);
        return false;
    }
};

// =========================================================================================
// SAFETY CHECK MODULE CLASS (從 SafetyCheckFSM.ino 提取)
// 實現了行為狀態機 (FSM) 邏輯
// =========================================================================================

struct QuizResult {
    std::string status;
    float accuracy;
    long reactionTime_ms;
    std::time_t timestamp;
};

class SafetyCheckModule {
private:
    State currentState;
    QuizResult currentResult;
    long stateStartTime;
    int patternLength;
    std::vector<int> requiredPattern;
    int currentPatternIndex;
    int userPressCount;
    int correctSequenceCount;
    long sequenceStartTime;

    const long ALERT_TIMEOUT_MS = 10000; 
    const long PATTERN_RESPONSE_TIMEOUT_MS = 20000; 
    const long MEASUREMENT_TIMEOUT_MS = 15000; 

    void transitionTo(State newState) {
        if (currentState == newState) return; 

        Serial.printf("\n================================================\n");
        Serial.printf("狀態轉移: %s -> %s\n", stateToString(currentState).c_str(), stateToString(newState).c_str());
        Serial.printf("================================================\n");
        
        currentState = newState;
        stateStartTime = millis(); 

        switch (newState) {
            case ALERTING: break;
            case PATTERN_DISPLAY:
                setupPattern();
                break;
            case WAITING_INPUT:
                sequenceStartTime = millis(); 
                break;
            case VITAL_MEASUREMENT: break;
            case COMPLETED:
                currentResult.status = "Completed";
                finalizeResult();
                break;
            case MEASUREMENT_FAILED:
                currentResult.status = "MeasurementFailed";
                break;
            case NO_RESPONSE:
                currentResult.status = "NoResponse";
                break;
            case STANDBY:
                userPressCount = 0;
                correctSequenceCount = 0;
                break;
        }
    }

    void handleStandby() {}
    void handleAlerting() {
        Serial.println(">>> [State Alerting] 系統正在提醒使用者進行檢查...");
    }
    void handlePatternDisplay() {
        if (currentPatternIndex < patternLength) {
            displayPattern(requiredPattern[currentPatternIndex]);
            currentPatternIndex++;
        } else {
            transitionTo(WAITING_INPUT);
        }
    }
    void handleWaitingInput() {
        if (digitalRead(BUTTON_PIN) == LOW) {
            userPressCount++;
            Serial.printf(">>> [Input] 按鈕已按下。計數: %d\n", userPressCount);

            if (userPressCount >= patternLength) { 
                correctSequenceCount++;
                Serial.println(">>> [Input] 記憶序列驗證成功！");
                userPressCount = 0;
                transitionTo(VITAL_MEASUREMENT);
            }
        }
    }
    void handleVitalMeasurement() {
        float hr = 0.0f, spo2 = 0.0f;
        bool success = readMax30100Vitals(hr, spo2);

        if (success) {
            latestHeartRate = hr;
            latestSpO2 = spo2;
            Serial.printf(">>> [Sensor] MAX30100 真實讀數獲取成功。HR: %.1f bpm, SpO2: %.1f%%\n", hr, spo2);
            transitionTo(COMPLETED);
        } else {
            Serial.println(">>> [Sensor] MAX30100 未取得有效心率/血氧讀數，量測失敗。");
            transitionTo(MEASUREMENT_FAILED);
        }
    }

    void setupPattern() {
        requiredPattern = {1, 3, 2, 4, 1}; 
        currentPatternIndex = 0;
        correctSequenceCount = 0;
    }

    long getTimeoutMS(State s) {
        switch (s) {
            case ALERTING: return ALERT_TIMEOUT_MS;
            case PATTERN_DISPLAY: return PATTERN_RESPONSE_TIMEOUT_MS; 
            case WAITING_INPUT: return PATTERN_RESPONSE_TIMEOUT_MS; 
            case VITAL_MEASUREMENT: return MEASUREMENT_TIMEOUT_MS;
            default: return 0;
        }
    }

    void finalizeResult() {
        long timeTaken = millis() - stateStartTime;
        
        currentResult.accuracy = (double)correctSequenceCount / patternLength; 
        currentResult.reactionTime_ms = timeTaken; 

        Serial.println("\n==================================================");
        Serial.println("[結果摘要]: 測驗已完成!");
        Serial.printf("   - 最終狀態: %s\n", currentResult.status.c_str());
        Serial.printf("   - 正確率: %.1f%%\n", currentResult.accuracy * 100.0);
        Serial.printf("   - 反應時間: %ld ms\n", currentResult.reactionTime_ms);
        Serial.printf("   - 最新 HR / SpO2: %.1f bpm / %.1f%%\n", latestHeartRate, latestSpO2);
        Serial.println("==================================================");
    }

    void displayPattern(int pattern_id) {
        Serial.printf(">> [Display] 正在展示記憶圖案 ID: %d\n", pattern_id);
    }

public:
    SafetyCheckModule() : 
        currentState(STANDBY), 
        patternLength(5), 
        currentPatternIndex(0), 
        userPressCount(0), 
        correctSequenceCount(0), 
        stateStartTime(0),
        sequenceStartTime(0) 
    {
        currentResult.timestamp = std::time(nullptr);
        currentResult.accuracy = 0.0;
        currentResult.reactionTime_ms = 0;
        currentResult.status = "Pending";
        Serial.println("--- SafetyCheckModule 初始化完成 (待機) ---");
    }

    void update() {
        if (currentState != STANDBY && stateStartTime != 0) {
            if (millis() - stateStartTime > getTimeoutMS(currentState)) {
                if (currentState != MEASUREMENT_FAILED && currentState != COMPLETED) {
                    transitionTo(NO_RESPONSE);
                }
                return; 
            }
        }
        
        switch (currentState) {
            case STANDBY: handleStandby(); break;
            case ALERTING: handleAlerting(); break;
            case PATTERN_DISPLAY: handlePatternDisplay(); break;
            case WAITING_INPUT: handleWaitingInput(); break;
            case VITAL_MEASUREMENT: handleVitalMeasurement(); break;
            case COMPLETED: break;
            case MEASUREMENT_FAILED: break;
            case NO_RESPONSE: break;
        }
    }
    
    void startTest() {
        transitionTo(ALERTING);
    }

    State getState() const { return currentState; }
    QuizResult getResult() const { return currentResult; }

    std::string stateToString(State s) {
        switch(s) {
            case STANDBY: return "STANDBY (待機)";
            case ALERTING: return "ALERTING (提醒中)";
            case PATTERN_DISPLAY: return "PATTERN_DISPLAY (圖案記憶展示)";
            case WAITING_INPUT: return "WAITING_INPUT (等待按鈕計數)";
            case VITAL_MEASUREMENT: return "VITAL_MEASUREMENT (生理量測)";
            case COMPLETED: return "COMPLETED (已完成)";
            case MEASUREMENT_FAILED: return "MEASUREMENT_FAILED (量測失敗)";
            case NO_RESPONSE: return "NO_RESPONSE (尚未回應)";
        }
        return "UNKNOWN";
    }
};

// --- 全域模組實例 ---
StorageManager storageManager;
SafetyCheckModule checker;
bool displayReady = false;

// =========================================================================================
// MAIN FUNCTION AND SETUP/LOOP
// =========================================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n========================================");
    Serial.println("  HeartCare Monitor System Initializing...");
    Serial.println("========================================");

    // 1. 檔案系統初始化
    if (!LittleFS.begin(true)) {
        Serial.println("WARNING: LittleFS 初始化與格式化失敗，暫以無檔案模式繼續啟動。");
    }
    
    // 2. 初始化 I2C 總線 (OLED)
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    
    // 3. 初始化 OLED 顯示器
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println("SSD1306 allocation failed");
        displayReady = false;
    } else {
        displayReady = true;
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.setCursor(0, 0);
        display.println("HeartCare Booting...");
        display.display();
    }

    // 4. 初始化 MAX30100 PulseOximeter
    poxReady = pox.begin();
    if (!poxReady) {
        Serial.println("WARNING: MAX30100 PulseOximeter 初始化失敗，請檢查 I2C 接線與供電。");
    } else {
        Serial.println("[MAX30100] PulseOximeter 初始化成功。");
    }

    // 5. 初始化感測器與輸入／輸出腳位
    dht.begin();
    pinMode(PHOTO_SENSITIVE_PIN, INPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP); 
    pinMode(HCSR04_TRIG_PIN, OUTPUT);
    pinMode(HCSR04_ECHO_PIN, INPUT);
    pinMode(BEEP_PIN, OUTPUT);
    digitalWrite(BEEP_PIN, LOW);
    
    // 6. 模組初始化與載入設定
    storageManager.begin();
    MedicationSetting currentMedSetting = storageManager.loadMedicationSetting();

    // 7. 初始狀態設置
    currentState = STATE_IDLE;
    if (displayReady) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.setCursor(0, 0);
        display.print("Status: IDLE (OK)");
        display.display();
    }

    Serial.println("Initializing Complete. System is IDLE.");
}

void loop() {
    unsigned long current_time = millis();

    // 持續更新 PulseOximeter 取樣
    if (poxReady) {
        pox.update();
    }

    // --- 按鈕測試觸發 ---
    static unsigned long lastButtonPress = 0;
    if (digitalRead(BUTTON_PIN) == LOW && (current_time - lastButtonPress > 300)) {
        lastButtonPress = current_time;
        if (checker.getState() == STANDBY) {
            Serial.println("[Button] 偵測到按鈕按下，啟動 SafetyCheck FSM！");
            checker.startTest();
            currentState = STATE_CONFIRM_SAFE;
        }
    }

    // --- A. 數據採集與狀態檢查 ---
    if (current_time - last_sensor_read_time >= SENSOR_READ_INTERVAL) {
        last_sensor_read_time = current_time;
        
        float temperature = dht.readTemperature();
        float humidity = dht.readHumidity();
        int lightRaw = analogRead(PHOTO_SENSITIVE_PIN);
        long distanceCm = readUltrasoundCm();
        float hr = 0.0f, spo2 = 0.0f;
        bool hrOk = readMax30100Vitals(hr, spo2);

        if (!isnan(temperature)) latestTempC = temperature;
        if (!isnan(humidity)) latestHumidity = humidity;
        latestLightRaw = lightRaw;
        latestDistanceCm = distanceCm;
        if (hrOk) {
            latestHeartRate = hr;
            latestSpO2 = spo2;
        }

        Serial.println("\n------------------ [Sensor Log] ------------------");
        Serial.printf("Uptime: %lu ms | SystemState: %d | FSM: %s\n",
                      current_time,
                      (int)currentState,
                      checker.stateToString(checker.getState()).c_str());

        if (isnan(temperature) || isnan(humidity)) {
            Serial.println("DHT22: 讀取失敗 (NaN)，請檢查接線與上拉電阻");
        } else {
            Serial.printf("DHT22: 溫度 = %.1f C | 濕度 = %.1f %%\n", temperature, humidity);
        }

        Serial.printf("光敏電阻 (GPIO %d): ADC = %d\n", PHOTO_SENSITIVE_PIN, lightRaw);

        if (distanceCm < 0) {
            Serial.println("HC-SR04: 未偵測到回波或逾時");
        } else {
            Serial.printf("HC-SR04: 距離 = %ld cm\n", distanceCm);
        }

        if (hrOk) {
            Serial.printf("MAX30100: HR = %.1f bpm | SpO2 = %.1f%%\n", hr, spo2);
        } else {
            Serial.println("MAX30100: 無有效心率/血氧讀數或尚未偵測到感測器");
        }
        Serial.println("--------------------------------------------------");

        if (!isnan(temperature) && temperature > 35.0 && currentState == STATE_IDLE) {
            Serial.println("⚠️ [Alert] 溫度超過 35.0 C，切換至 STATE_ALERT！");
            currentState = STATE_ALERT;
        }
    }
    
    // --- B. 業務邏輯狀態更新 (SafetyCheck FSM) ---
    checker.update();

    // --- C. 動作與顯示 ---
    digitalWrite(BEEP_PIN, (currentState == STATE_ALERT) ? HIGH : LOW);

    if (displayReady && (current_time - last_display_update_time >= DISPLAY_UPDATE_INTERVAL)) {
        last_display_update_time = current_time;

        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(WHITE);

        String status_msg;
        if (currentState == STATE_IDLE) {
            status_msg = "Status: IDLE (OK)";
        } else if (currentState == STATE_ALERT) {
            status_msg = "!!! WARNING: ALERT !!!"; 
        } else {
            status_msg = String(checker.stateToString(checker.getState()).c_str());
        }

        display.setCursor(0, 0);
        display.println(status_msg);

        display.setCursor(0, 16);
        if (!isnan(latestTempC) && !isnan(latestHumidity)) {
            display.printf("T:%.1fC  H:%.0f%%", latestTempC, latestHumidity);
        } else {
            display.print("T:--.-C  H:--%");
        }

        display.setCursor(0, 32);
        display.printf("L:%d D:%ldcm HR:%.0f", latestLightRaw, latestDistanceCm, latestHeartRate);

        display.setCursor(0, 50);
        display.print("Btn:Test  RealData");
        display.display();
    }

    // --- D. 後臺任務: 佇列處理 (StorageManager) ---
    static unsigned long lastQueueCheck = 0;
    if (current_time - lastQueueCheck > 5000) {
        storageManager.processPendingQueue();
        lastQueueCheck = current_time;
    }

    delay(10); 
}