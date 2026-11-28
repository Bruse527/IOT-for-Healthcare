// ============================================================================
// Healthcare IoT - Integrated Main Sketch
// ESP32 NodeMCU-32S
//
// 功能：
// 1. OLED 圖案出現次數記憶測驗
// 2. 單一按鈕：短按啟動/計數、長按確認
// 3. 超音波自動偵測靠近 (持續 3 秒自動觸發生理量測 + 30 秒冷卻防呆)
// 4. MAX30100 心率 / 血氧量測
// 5. DHT22 溫濕度監測
// 6. 光敏感測器 & 蜂鳴器異常提醒
// 7. Preferences 用藥設定儲存 & LittleFS 日誌與離線事件佇列
// 8. Wi-Fi / GAS 定時狀態上報與異常事件上傳
// ============================================================================

// ============================================================================
// 01. LIBRARIES
// ============================================================================
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MAX30100_PulseOximeter.h>
#include <DHT.h>

#include <Preferences.h>
#include <FS.h>
#include <LittleFS.h>

#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include <vector>
#include <math.h>

// ============================================================================
// 02. USER CONFIGURATION (請填入您的 Wi-Fi 與新部署的 GAS_URL)
// ============================================================================
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* GAS_URL =
    "https://script.google.com/macros/s/請替換為重新部署後的_GAS_DEPLOYMENT_ID/exec";

const bool CLOUD_ENABLED = true;

// 蜂鳴器類型設定 (有源蜂鳴器填 false，無源蜂鳴器填 true)
const bool PASSIVE_BUZZER = false;

// 超音波偵測靠近、量測穩定時間與定時上報設定
const long PRESENCE_THRESHOLD_CM = 50;                // 靠近門檻 (公分)
const unsigned long PRESENCE_CONFIRM_MS = 3000;       // 持續靠近 3 秒觸發
const unsigned long PRESENCE_COOLDOWN_MS = 30000;     // 量測後冷卻 30 秒
const unsigned long VITAL_STABILIZE_MS = 3000;        // 量測前 3 秒穩定採樣期 (防讀舊值)
const unsigned long PERIODIC_REPORT_INTERVAL = 60000; // 定時上報間隔 (60 秒)
const int MAX_UPLOAD_RETRIES = 5;                     // 單筆離線事件最多重試 5 次

// ============================================================================
// 03. HARDWARE PIN DEFINITIONS
// ============================================================================
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

// ============================================================================
// 04. DISPLAY AND SENSOR OBJECTS
// ============================================================================
TwoWire OLED_Wire(1);
Adafruit_SSD1306 display(128, 64, &OLED_Wire, -1);
PulseOximeter pox;
DHT dht(DHT22_PIN, DHT22);

bool displayReady = false;
bool poxReady = false;
bool littleFSReady = false;

// ============================================================================
// 05. SENSOR DATA
// ============================================================================
float latestTempC = NAN;
float latestHumidity = NAN;
int latestLightRaw = 0;
long latestDistanceCm = -1;
float latestHeartRate = 0.0f;
float latestSpO2 = 0.0f;

bool temperatureAlert = false;
bool temperatureAlertEventQueued = false;

// ============================================================================
// 06. SYSTEM TIMING
// ============================================================================
unsigned long lastSensorReadTime = 0;
const unsigned long SENSOR_READ_INTERVAL = 2000;

unsigned long lastDisplayUpdateTime = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 300;

unsigned long lastWiFiCheckTime = 0;
const unsigned long WIFI_CHECK_INTERVAL = 10000;

unsigned long lastQueueCheckTime = 0;
const unsigned long QUEUE_CHECK_INTERVAL = 10000;

unsigned long lastPeriodicReportTime = 0;
unsigned long lastPresenceEndTime = 0;
bool presenceCooldownActive = false;

// ============================================================================
// 07 & 08. SYSTEM & SAFETY FSM STATES
// ============================================================================
enum SystemState {
    STATE_IDLE,
    STATE_ALERT,
    STATE_CONFIRM_SAFE
};
SystemState currentSystemState = STATE_IDLE;

enum SafetyState {
    STANDBY,
    DETECTING_PRESENCE,
    ALERTING,
    PATTERN_DISPLAY,
    WAITING_INPUT,
    VITAL_MEASUREMENT,
    COMPLETED,
    MEASUREMENT_FAILED,
    NO_RESPONSE
};
SafetyState currentSafetyState = STANDBY;

bool quizTriggeredThisSession = false;
bool presenceTriggeredThisSession = false;

void transitionTo(SafetyState newState);

// ============================================================================
// 09. MEMORY QUIZ SETTINGS
// ============================================================================
enum PatternType {
    PATTERN_STAR = 0,
    PATTERN_HEART,
    PATTERN_CIRCLE,
    PATTERN_SQUARE,
    PATTERN_TRIANGLE,
    PATTERN_COUNT
};

const int MAX_PATTERN_LENGTH = 12;
int patternSequence[MAX_PATTERN_LENGTH];
int patternLength = 12;
int currentPatternIndex = 0;

int targetPattern = 0;
int correctAnswer = 0;
int userAnswer = 0;
bool quizCorrect = false;

const unsigned long PATTERN_SHOW_TIME = 900;
const unsigned long PATTERN_GAP_TIME = 350;
const unsigned long QUESTION_TIMEOUT = 30000;
const unsigned long ALERT_DURATION = 1500;
const unsigned long MEASUREMENT_TIMEOUT = 15000;
const unsigned long RESULT_DISPLAY_TIME = 5000;

unsigned long patternTimer = 0;
unsigned long stateStartTime = 0;
unsigned long questionStartTime = 0;
unsigned long quizStartTime = 0;
unsigned long reactionTime = 0;
bool patternVisible = false;

// ============================================================================
// 10. BUTTON CONTROL
// ============================================================================
const unsigned long BUTTON_DEBOUNCE_MS = 40;
const unsigned long BUTTON_LONG_PRESS_MS = 2000;

bool buttonRawState = HIGH;
bool buttonStableState = HIGH;
unsigned long buttonDebounceTime = 0;
unsigned long buttonPressStartTime = 0;
bool longPressTriggered = false;

bool shortPressEvent = false;
bool longPressEvent = false;

// ============================================================================
// 11. STORAGE STRUCTURES
// ============================================================================
struct MedicationSetting {
    char name[32];
    char time[16];
    char instruction[64];
    bool isRepeating;
    bool isEnabled;
    bool isActive;
};

struct LogEntry {
    unsigned long timestamp;
    char entryType[24];
    float value1;
    char details[96];
};

struct PendingEvent {
    unsigned long timestamp;
    char eventType[32];
    String payload;
    int attemptCount;
};

// ============================================================================
// 12. STORAGE MANAGER (調整 MAX_QUEUE_SIZE = 15 確保記憶體安全)
// ============================================================================
class StorageManager {
private:
    Preferences preferences;
    const char* LOG_FILE_PATH = "/daily_logs.json";
    const char* QUEUE_FILE_PATH = "/pending_queue.json";
    const int MAX_LOG_ENTRIES = 100;
    const int MAX_QUEUE_SIZE = 15;

public:
    bool begin() {
        if (!LittleFS.begin(true)) {
            Serial.println("[LittleFS] 初始化失敗");
            return false;
        }
        Serial.println("[LittleFS] 初始化成功");
        return true;
    }

    void saveMedicationSetting(const MedicationSetting& setting) {
        preferences.begin("app_config", false);
        preferences.putString("med_name", setting.name);
        preferences.putString("med_time", setting.time);
        preferences.putString("med_instruction", setting.instruction);
        preferences.putBool("med_enabled", setting.isEnabled);
        preferences.putBool("med_repeating", setting.isRepeating);
        preferences.putBool("med_active", setting.isActive);
        preferences.end();
        Serial.println("[NVS] 用藥設定已儲存");
    }

    MedicationSetting loadMedicationSetting() {
        MedicationSetting setting = {};
        preferences.begin("app_config", true);
        preferences.getString("med_name", setting.name, sizeof(setting.name));
        preferences.getString("med_time", setting.time, sizeof(setting.time));
        preferences.getString("med_instruction", setting.instruction, sizeof(setting.instruction));
        setting.isEnabled = preferences.getBool("med_enabled", false);
        setting.isRepeating = preferences.getBool("med_repeating", false);
        setting.isActive = preferences.getBool("med_active", false);
        preferences.end();
        return setting;
    }

    void saveLog(const char* type, float value, const char* details) {
        if (!littleFSReady) return;

        DynamicJsonDocument doc(12288);
        File file = LittleFS.open(LOG_FILE_PATH, "r");
        if (file) {
            DeserializationError error = deserializeJson(doc, file);
            file.close();
            if (error || !doc.is<JsonArray>()) doc.clear();
        }
        if (!doc.is<JsonArray>()) doc.to<JsonArray>();

        JsonArray arr = doc.as<JsonArray>();
        if (arr.size() >= MAX_LOG_ENTRIES) arr.remove(0);

        JsonObject obj = arr.createNestedObject();
        obj["timestamp"] = millis();
        obj["type"] = type;
        obj["value"] = isnan(value) ? 0.0f : value;
        obj["details"] = details;

        File output = LittleFS.open(LOG_FILE_PATH, "w");
        if (!output) {
            Serial.println("[LittleFS] 日誌寫入失敗");
            return;
        }
        serializeJson(doc, output);
        output.close();
    }

    void queuePendingEvent(const char* eventType, const String& payload) {
        if (!littleFSReady) return;

        DynamicJsonDocument doc(12288);
        File file = LittleFS.open(QUEUE_FILE_PATH, "r");
        if (file) {
            DeserializationError error = deserializeJson(doc, file);
            file.close();
            if (error || !doc.is<JsonArray>()) doc.clear();
        }
        if (!doc.is<JsonArray>()) doc.to<JsonArray>();

        JsonArray arr = doc.as<JsonArray>();
        if (arr.size() >= MAX_QUEUE_SIZE) {
            arr.remove(0);
            Serial.println("[Queue] 佇列已滿，移除最舊事件");
        }

        JsonObject obj = arr.createNestedObject();
        obj["timestamp"] = millis();
        obj["eventType"] = eventType;
        obj["payload"] = payload;
        obj["attemptCount"] = 0;

        File output = LittleFS.open(QUEUE_FILE_PATH, "w");
        if (!output) {
            Serial.println("[Queue] 寫入失敗");
            return;
        }
        serializeJson(doc, output);
        output.close();
        Serial.printf("[Queue] 已加入事件：%s\n", eventType);
    }

    bool getFirstPendingEvent(String& eventType, String& payload, int& attempts) {
        if (!littleFSReady) return false;
        File file = LittleFS.open(QUEUE_FILE_PATH, "r");
        if (!file) return false;

        DynamicJsonDocument doc(12288);
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        if (error || !doc.is<JsonArray>()) return false;

        JsonArray arr = doc.as<JsonArray>();
        if (arr.size() == 0) return false;

        JsonObject obj = arr[0];
        eventType = obj["eventType"].as<String>();
        payload = obj["payload"].as<String>();
        attempts = obj["attemptCount"] | 0;
        return true;
    }

    void removeFirstPendingEvent() {
        if (!littleFSReady) return;
        File file = LittleFS.open(QUEUE_FILE_PATH, "r");
        if (!file) return;

        DynamicJsonDocument doc(12288);
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        if (error || !doc.is<JsonArray>()) return;

        JsonArray arr = doc.as<JsonArray>();
        if (arr.size() > 0) arr.remove(0);

        File output = LittleFS.open(QUEUE_FILE_PATH, "w");
        if (!output) return;
        serializeJson(doc, output);
        output.close();
    }

    void increaseFirstAttempt() {
        if (!littleFSReady) return;
        File file = LittleFS.open(QUEUE_FILE_PATH, "r");
        if (!file) return;

        DynamicJsonDocument doc(12288);
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        if (error || !doc.is<JsonArray>()) return;

        JsonArray arr = doc.as<JsonArray>();
        if (arr.size() == 0) return;

        int attempts = arr[0]["attemptCount"] | 0;
        arr[0]["attemptCount"] = attempts + 1;

        File output = LittleFS.open(QUEUE_FILE_PATH, "w");
        if (!output) return;
        serializeJson(doc, output);
        output.close();
    }

    bool hasPendingEvents() {
        if (!littleFSReady) return false;
        File file = LittleFS.open(QUEUE_FILE_PATH, "r");
        if (!file) return false;

        DynamicJsonDocument doc(12288);
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        if (error || !doc.is<JsonArray>()) return false;
        return doc.as<JsonArray>().size() > 0;
    }
};

StorageManager storageManager;

// ============================================================================
// 13. BUTTON & BUZZER HELPERS
// ============================================================================
void setBuzzer(bool on) {
    if (PASSIVE_BUZZER) {
        if (on) tone(BEEP_PIN, 2000);
        else noTone(BEEP_PIN);
    } else {
        digitalWrite(BEEP_PIN, on ? HIGH : LOW);
    }
}

void updateButton() {
    shortPressEvent = false;
    longPressEvent = false;

    bool reading = digitalRead(BUTTON_PIN);
    if (reading != buttonRawState) {
        buttonDebounceTime = millis();
        buttonRawState = reading;
    }

    if (millis() - buttonDebounceTime >= BUTTON_DEBOUNCE_MS) {
        if (reading != buttonStableState) {
            buttonStableState = reading;
            if (buttonStableState == LOW) {
                buttonPressStartTime = millis();
                longPressTriggered = false;
            } else {
                if (!longPressTriggered) shortPressEvent = true;
            }
        }
    }

    if (buttonStableState == LOW && !longPressTriggered &&
        millis() - buttonPressStartTime >= BUTTON_LONG_PRESS_MS) {
        longPressTriggered = true;
        longPressEvent = true;
    }
}

// ============================================================================
// 14 & 15. SENSORS
// ============================================================================
long readUltrasoundCm() {
    digitalWrite(HCSR04_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(HCSR04_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(HCSR04_TRIG_PIN, LOW);

    long duration = pulseIn(HCSR04_ECHO_PIN, HIGH, 30000);
    if (duration <= 0) return -1;
    return (long)(duration * 0.0343 / 2.0);
}

bool readMax30100Vitals(float& hrOut, float& spo2Out) {
    if (!poxReady) return false;

    float hr = pox.getHeartRate();
    float spo2 = pox.getSpO2();

    if (hr >= 30.0 && hr <= 220.0 && spo2 >= 70.0 && spo2 <= 100.0) {
        hrOut = hr;
        spo2Out = spo2;
        return true;
    }
    return false;
}

// ============================================================================
// 16 & 17. OLED DRAWING & MESSAGE FUNCTIONS
// ============================================================================
void drawStar(int cx, int cy) {
    display.drawLine(cx, cy - 14, cx + 4, cy - 4, WHITE);
    display.drawLine(cx + 4, cy - 4, cx + 14, cy - 4, WHITE);
    display.drawLine(cx + 14, cy - 4, cx + 6, cy + 3, WHITE);
    display.drawLine(cx + 6, cy + 3, cx + 9, cy + 14, WHITE);
    display.drawLine(cx + 9, cy + 14, cx, cy + 7, WHITE);
    display.drawLine(cx, cy + 7, cx - 9, cy + 14, WHITE);
    display.drawLine(cx - 9, cy + 14, cx - 6, cy + 3, WHITE);
    display.drawLine(cx - 6, cy + 3, cx - 14, cy - 4, WHITE);
    display.drawLine(cx - 14, cy - 4, cx - 4, cy - 4, WHITE);
    display.drawLine(cx - 4, cy - 4, cx, cy - 14, WHITE);
}

void drawHeart(int cx, int cy) {
    display.fillCircle(cx - 5, cy - 4, 7, WHITE);
    display.fillCircle(cx + 5, cy - 4, 7, WHITE);
    display.fillTriangle(cx - 12, cy - 1, cx + 12, cy - 1, cx, cy + 15, WHITE);
}

void drawCircle(int cx, int cy) {
    display.drawCircle(cx, cy, 14, WHITE);
}

void drawSquare(int cx, int cy) {
    display.drawRect(cx - 12, cy - 12, 24, 24, WHITE);
}

void drawTriangle(int cx, int cy) {
    display.drawTriangle(cx, cy - 14, cx - 14, cy + 12, cx + 14, cy + 12, WHITE);
}

void drawPattern(int pattern) {
    if (!displayReady) return;
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Memory Test");

    int cx = 64, cy = 37;
    switch (pattern) {
        case PATTERN_STAR:     drawStar(cx, cy); break;
        case PATTERN_HEART:    drawHeart(cx, cy); break;
        case PATTERN_CIRCLE:   drawCircle(cx, cy); break;
        case PATTERN_SQUARE:   drawSquare(cx, cy); break;
        case PATTERN_TRIANGLE: drawTriangle(cx, cy); break;
    }
    display.display();
}

void showMessage(const char* line1, const char* line2 = "", const char* line3 = "") {
    if (!displayReady) return;
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 10); display.println(line1);
    display.setCursor(0, 28); display.println(line2);
    display.setCursor(0, 46); display.println(line3);
    display.display();
}

void showQuestion() {
    if (!displayReady) return;
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("How many times?");

    int cx = 28, cy = 36;
    switch (targetPattern) {
        case PATTERN_STAR:     drawStar(cx, cy); break;
        case PATTERN_HEART:    drawHeart(cx, cy); break;
        case PATTERN_CIRCLE:   drawCircle(cx, cy); break;
        case PATTERN_SQUARE:   drawSquare(cx, cy); break;
        case PATTERN_TRIANGLE: drawTriangle(cx, cy); break;
    }

    display.setTextSize(1);
    display.setCursor(72, 23); display.println("Answer:");
    display.setTextSize(3);
    display.setCursor(78, 36); display.print(userAnswer);
    display.setTextSize(1);
    display.setCursor(0, 56); display.println("Short:+1  Hold:OK");
    display.display();
}

// ============================================================================
// 18. MEMORY QUIZ FUNCTIONS
// ============================================================================
void generatePattern() {
    correctAnswer = 0;
    userAnswer = 0;
    currentPatternIndex = 0;
    patternVisible = false;
    patternTimer = millis() - PATTERN_GAP_TIME;

    for (int i = 0; i < patternLength; i++) {
        patternSequence[i] = random(0, PATTERN_COUNT);
    }
    targetPattern = random(0, PATTERN_COUNT);

    for (int i = 0; i < patternLength; i++) {
        if (patternSequence[i] == targetPattern) correctAnswer++;
    }

    Serial.println("\n[Quiz] 新測驗開始");
    Serial.printf("[Quiz] 目標圖案：%d | 正確答案：%d\n", targetPattern, correctAnswer);
}

void updatePatternDisplay() {
    unsigned long now = millis();
    if (currentPatternIndex >= patternLength) {
        transitionTo(WAITING_INPUT);
        return;
    }

    if (!patternVisible) {
        if (now - patternTimer >= PATTERN_GAP_TIME) {
            drawPattern(patternSequence[currentPatternIndex]);
            patternVisible = true;
            patternTimer = now;
        }
    } else if (now - patternTimer >= PATTERN_SHOW_TIME) {
        showMessage("Remember the", "pictures...");
        patternVisible = false;
        patternTimer = now;
        currentPatternIndex++;
    }
}

// ============================================================================
// 19 & 20. EVENT JSON GENERATION & CLOUD SYNC
// ============================================================================
String createEventPayload(const char* eventType) {
    DynamicJsonDocument doc(1024);

    doc["deviceId"] = "HeartCare_ESP32_01";
    doc["event_type"] = eventType;
    doc["uptimeMs"] = millis();

    if (isnan(latestTempC)) doc["temperature"] = nullptr;
    else doc["temperature"] = latestTempC;

    if (isnan(latestHumidity)) doc["humidity"] = nullptr;
    else doc["humidity"] = latestHumidity;

    doc["lightLevel"] = latestLightRaw;
    doc["distanceCm"] = latestDistanceCm;
    doc["heartRate"] = latestHeartRate;
    doc["spo2"] = latestSpO2;

    doc["quizCorrect"] = quizCorrect;
    doc["userAnswer"] = userAnswer;
    doc["correctAnswer"] = correctAnswer;
    doc["quizReactionMs"] = reactionTime;
    doc["battery"] = 100;

    String payload;
    serializeJson(doc, payload);
    return payload;
}

void createCloudEvent(const char* eventType) {
    if (!CLOUD_ENABLED) return;
    String payload = createEventPayload(eventType);
    storageManager.queuePendingEvent(eventType, payload);
}

bool sendCloudPayload(const String& payload) {
    if (!CLOUD_ENABLED || WiFi.status() != WL_CONNECTED) return false;
    if (String(GAS_URL).indexOf("YOUR_DEPLOYMENT_ID") >= 0 ||
        String(GAS_URL).indexOf("請替換") >= 0) {
        Serial.println("[Cloud] 尚未設定有效的 GAS URL");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient https;
    if (!https.begin(client, GAS_URL)) {
        Serial.println("[Cloud] HTTPS 初始化失敗");
        return false;
    }

    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    https.addHeader("Content-Type", "application/json");

    int httpCode = https.POST(payload);
    String response = https.getString();
    https.end();

    Serial.printf("[Cloud] HTTP Code: %d\n", httpCode);
    if (httpCode >= 200 && httpCode < 300) {
        Serial.println("[Cloud] 傳送成功: " + response);
        return true;
    }

    Serial.println("[Cloud] 傳送失敗");
    return false;
}

void processPendingQueue() {
    if (!CLOUD_ENABLED || WiFi.status() != WL_CONNECTED) return;
    if (!storageManager.hasPendingEvents()) return;

    String eventType, payload;
    int attempts = 0;
    if (!storageManager.getFirstPendingEvent(eventType, payload, attempts)) return;

    Serial.printf("[Queue] 正在補傳：%s (第 %d 次嘗試)\n", eventType.c_str(), attempts + 1);
    if (sendCloudPayload(payload)) {
        storageManager.removeFirstPendingEvent();
    } else {
        // 連網狀態下重試達上限後自動略過，避免卡死後續新事件
        if (attempts + 1 >= MAX_UPLOAD_RETRIES) {
            Serial.printf("[Queue] 事件 %s 已失敗 %d 次，自動略過移除\n",
                          eventType.c_str(), MAX_UPLOAD_RETRIES);
            storageManager.removeFirstPendingEvent();
        } else {
            storageManager.increaseFirstAttempt();
        }
    }
}

void updatePeriodicReport() {
    if (!CLOUD_ENABLED) return;
    if (currentSafetyState != STANDBY) return;

    unsigned long now = millis();
    if (now - lastPeriodicReportTime >= PERIODIC_REPORT_INTERVAL) {
        lastPeriodicReportTime = now;
        Serial.println("[Cloud] 定時狀態上報 (periodic_status)");
        createCloudEvent("periodic_status");
    }
}

// ============================================================================
// 21. SENSOR MONITORING
// ============================================================================
void updateSensors() {
    if (currentSafetyState == VITAL_MEASUREMENT || currentSafetyState == DETECTING_PRESENCE) {
        return;
    }

    unsigned long now = millis();
    if (now - lastSensorReadTime < SENSOR_READ_INTERVAL) return;
    lastSensorReadTime = now;

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    if (!isnan(temperature)) latestTempC = temperature;
    if (!isnan(humidity)) latestHumidity = humidity;

    latestLightRaw = analogRead(PHOTO_SENSITIVE_PIN);
    latestDistanceCm = readUltrasoundCm();

    float hr = 0, spo2 = 0;
    if (readMax30100Vitals(hr, spo2)) {
        latestHeartRate = hr;
        latestSpO2 = spo2;
    }

    temperatureAlert = (!isnan(latestTempC) && latestTempC > 35.0);

    Serial.println("\n========== Sensor Data ==========");
    if (!isnan(latestTempC)) Serial.printf("Temperature: %.1f C\n", latestTempC);
    else Serial.println("Temperature: ERROR");

    if (!isnan(latestHumidity)) Serial.printf("Humidity: %.1f %%\n", latestHumidity);
    else Serial.println("Humidity: ERROR");

    Serial.printf("Light: %d\n", latestLightRaw);
    Serial.printf("Distance: %ld cm\n", latestDistanceCm);
    Serial.printf("Heart Rate: %.1f bpm\n", latestHeartRate);
    Serial.printf("SpO2: %.1f %%\n", latestSpO2);
    Serial.println("=================================");

    storageManager.saveLog("Sensor", latestTempC, "Periodic sensor reading");

    if (temperatureAlert) {
        if (!temperatureAlertEventQueued) {
            Serial.println("[Alert] 溫度超過 35 C");
            storageManager.saveLog("TemperatureAlert", latestTempC, "Temperature above 35 C");
            createCloudEvent("temperature_alert");
            temperatureAlertEventQueued = true;
        }
    } else {
        temperatureAlertEventQueued = false;
    }
}

// ============================================================================
// 22. WIFI MANAGEMENT
// ============================================================================
void connectWiFi() {
    if (!CLOUD_ENABLED) {
        Serial.println("[WiFi] 雲端功能尚未啟用");
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.println("[WiFi] 正在連線...");
}

void updateWiFi() {
    if (!CLOUD_ENABLED) return;
    if (millis() - lastWiFiCheckTime < WIFI_CHECK_INTERVAL) return;
    lastWiFiCheckTime = millis();

    if (WiFi.status() == WL_CONNECTED) return;

    Serial.println("[WiFi] 連線中斷，重新連線");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// ============================================================================
// 23. SAFETY FSM
// ============================================================================
void transitionTo(SafetyState newState) {
    if (currentSafetyState == newState) return;
    Serial.printf("[FSM] %d -> %d\n", currentSafetyState, newState);

    currentSafetyState = newState;
    stateStartTime = millis();

    switch (newState) {
        case STANDBY:
            currentSystemState = STATE_IDLE;
            break;

        case DETECTING_PRESENCE:
            currentSystemState = STATE_IDLE;
            showMessage("User Detected", "Hold still 3s...", "Auto Vital Check");
            break;

        case ALERTING:
            currentSystemState = STATE_CONFIRM_SAFE;
            break;

        case PATTERN_DISPLAY:
            generatePattern();
            break;

        case WAITING_INPUT:
            userAnswer = 0;
            questionStartTime = millis();
            showQuestion();
            break;

        case VITAL_MEASUREMENT:
            showMessage("Measuring...", "Place finger on", "sensor & stay still");
            break;

        case COMPLETED:
            showMessage("Check Completed", "Data uploaded!", "Thank you!");
            break;

        case MEASUREMENT_FAILED:
            showMessage("Measurement", "Timeout / Failed", "Try again later");
            break;

        case NO_RESPONSE:
            showMessage("No Response", "Please check");
            break;
    }
}

void startSafetyCheck() {
    quizStartTime = millis();
    quizCorrect = false;
    reactionTime = 0;
    quizTriggeredThisSession = true;
    presenceTriggeredThisSession = false;

    Serial.println("[Safety] 開始平安確認 (按鈕觸發)");
    storageManager.saveLog("SafetyCheckStart", 0, "Safety check started");
    transitionTo(ALERTING);
}

void handleDetectingPresence() {
    static unsigned long lastPingTime = 0;
    static unsigned long lastScreenRefresh = 0;
    unsigned long now = millis();

    if (shortPressEvent) {
        shortPressEvent = false;
        startSafetyCheck();
        return;
    }

    if (now - lastPingTime >= 250) {
        lastPingTime = now;
        latestDistanceCm = readUltrasoundCm();
    }

    if (latestDistanceCm <= 0 || latestDistanceCm > PRESENCE_THRESHOLD_CM) {
        Serial.println("[FSM] DETECTING_PRESENCE -> STANDBY: 使用者離開範圍");
        transitionTo(STANDBY);
        return;
    }

    if (now - lastScreenRefresh >= 400) {
        lastScreenRefresh = now;
        unsigned long elapsed = now - stateStartTime;
        int remainSec = (elapsed < PRESENCE_CONFIRM_MS)
            ? (int)((PRESENCE_CONFIRM_MS - elapsed + 999) / 1000) : 0;

        char line2[32], line3[32];
        snprintf(line2, sizeof(line2), "Starting in %ds...", remainSec);
        snprintf(line3, sizeof(line3), "Dist: %ld cm", latestDistanceCm);
        showMessage("User Detected", line2, line3);
    }

    if (now - stateStartTime >= PRESENCE_CONFIRM_MS) {
        Serial.println("[FSM] DETECTING_PRESENCE -> VITAL_MEASUREMENT: 存在確認，啟動自動量測");
        quizTriggeredThisSession = false;
        presenceTriggeredThisSession = true;
        storageManager.saveLog("PresenceDetected", (float)latestDistanceCm, "Auto vital check triggered");
        transitionTo(VITAL_MEASUREMENT);
    }
}

void handleAlerting() {
    setBuzzer(true);
    if (millis() - stateStartTime >= ALERT_DURATION) {
        setBuzzer(false);
        transitionTo(PATTERN_DISPLAY);
    }
}

void handlePatternDisplay() {
    updatePatternDisplay();
}

void handleWaitingInput() {
    if (millis() - questionStartTime >= QUESTION_TIMEOUT) {
        transitionTo(NO_RESPONSE);
        return;
    }

    if (shortPressEvent) {
        userAnswer++;
        if (userAnswer > patternLength) userAnswer = patternLength;
        showQuestion();
        Serial.printf("[Quiz] 使用者答案：%d\n", userAnswer);
        shortPressEvent = false;
    }

    if (longPressEvent) {
        reactionTime = millis() - questionStartTime;
        quizCorrect = (userAnswer == correctAnswer);

        Serial.printf("[Quiz] 正確答案：%d | 使用者答案：%d | 結果：%s\n",
                      correctAnswer, userAnswer, quizCorrect ? "CORRECT" : "INCORRECT");

        storageManager.saveLog("MemoryQuiz", quizCorrect ? 1 : 0,
                               quizCorrect ? "Memory quiz correct" : "Memory quiz incorrect");
        createCloudEvent("memory_quiz");

        transitionTo(VITAL_MEASUREMENT);
        longPressEvent = false;
    }
}

void handleVitalMeasurement() {
    unsigned long elapsed = millis() - stateStartTime;

    // 強制等待前 3 秒穩定採樣期，避免讀到 MAX30100 上一次暫存的舊值
    if (elapsed < VITAL_STABILIZE_MS) {
        return;
    }

    float hr = 0, spo2 = 0;
    if (readMax30100Vitals(hr, spo2)) {
        latestHeartRate = hr;
        latestSpO2 = spo2;
        Serial.printf("[Safety] 生理量測成功 HR: %.1f bpm | SpO2: %.1f %%\n", hr, spo2);

        storageManager.saveLog("VitalMeasurement", hr, "Heart rate measured");
        storageManager.saveLog("SpO2", spo2, "Blood oxygen measured");

        if (quizTriggeredThisSession) {
            createCloudEvent("safety_check_completed");
        } else {
            createCloudEvent("presence_vital_measured");
        }

        transitionTo(COMPLETED);
        return;
    }

    if (elapsed >= MEASUREMENT_TIMEOUT) {
        Serial.println("[Safety] MAX30100 量測逾時");
        transitionTo(MEASUREMENT_FAILED);
    }
}

void handleResultState() {
    if (millis() - stateStartTime < RESULT_DISPLAY_TIME) return;

    if (currentSafetyState == COMPLETED) {
        storageManager.saveLog("SafetyCheckCompleted", quizCorrect ? 1 : 0, "Check completed");
    } else if (currentSafetyState == MEASUREMENT_FAILED) {
        storageManager.saveLog("MeasurementFailed", 0, "MAX30100 measurement failed");
        createCloudEvent("measurement_failed");
    } else if (currentSafetyState == NO_RESPONSE) {
        storageManager.saveLog("NoResponse", 0, "User did not complete safety check");
        createCloudEvent("no_response");
    }

    if (presenceTriggeredThisSession) {
        lastPresenceEndTime = millis();
        presenceCooldownActive = true;
        presenceTriggeredThisSession = false;
    }
    quizTriggeredThisSession = false;

    transitionTo(STANDBY);
}

void updateSafetyFSM() {
    switch (currentSafetyState) {
        case STANDBY:
            if (presenceCooldownActive && (millis() - lastPresenceEndTime >= PRESENCE_COOLDOWN_MS)) {
                presenceCooldownActive = false;
                Serial.println("[FSM] 超音波靠近冷卻結束，恢復自動偵測");
            }

            if (shortPressEvent) {
                shortPressEvent = false;
                startSafetyCheck();
                break;
            }

            if (!presenceCooldownActive &&
                latestDistanceCm > 0 &&
                latestDistanceCm <= PRESENCE_THRESHOLD_CM) {
                Serial.printf("[FSM] STANDBY -> DETECTING_PRESENCE (Dist: %ld cm)\n", latestDistanceCm);
                transitionTo(DETECTING_PRESENCE);
            }
            break;

        case DETECTING_PRESENCE:
            handleDetectingPresence();
            break;
        case ALERTING:
            handleAlerting();
            break;
        case PATTERN_DISPLAY:
            handlePatternDisplay();
            break;
        case WAITING_INPUT:
            handleWaitingInput();
            break;
        case VITAL_MEASUREMENT:
            handleVitalMeasurement();
            break;
        case COMPLETED:
        case MEASUREMENT_FAILED:
        case NO_RESPONSE:
            handleResultState();
            break;
    }
}

// ============================================================================
// 24 & 25. BUZZER & OLED STATUS DISPLAY
// ============================================================================
void updateBuzzer() {
    if (currentSafetyState == ALERTING) return;

    if (temperatureAlert) {
        // 間歇鳴叫：每 2 秒響 0.3 秒，降低長鳴干擾
        bool beepOn = (millis() % 2000) < 300;
        setBuzzer(beepOn);
        return;
    }

    setBuzzer(false);
}

void updateStatusDisplay() {
    if (!displayReady) return;
    if (millis() - lastDisplayUpdateTime < DISPLAY_UPDATE_INTERVAL) return;
    lastDisplayUpdateTime = millis();

    if (currentSafetyState != STANDBY) return;

    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println("HeartCare Monitor");

    display.setCursor(0, 13);
    if (temperatureAlert) {
        display.println("WARNING: TEMP HIGH");
    } else if (presenceCooldownActive) {
        display.println("Status: COOLDOWN");
    } else {
        display.println("Status: IDLE");
    }

    display.setCursor(0, 27);
    if (!isnan(latestTempC)) {
        display.printf("T:%.1fC H:%.0f%%", latestTempC, latestHumidity);
    } else {
        display.println("T:--.-C H:--%");
    }

    display.setCursor(0, 41);
    display.printf("HR:%.0f SpO2:%.0f", latestHeartRate, latestSpO2);

    display.setCursor(0, 55);
    display.println("Btn:Quiz | Near:Check");
    display.display();
}

// ============================================================================
// 26. SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("====================================");
    Serial.println("      HeartCare IoT System");
    Serial.println("====================================");

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(BEEP_PIN, OUTPUT);
    pinMode(HCSR04_TRIG_PIN, OUTPUT);
    pinMode(HCSR04_ECHO_PIN, INPUT);
    pinMode(PHOTO_SENSITIVE_PIN, INPUT);
    setBuzzer(false);

    randomSeed(analogRead(PHOTO_SENSITIVE_PIN) + micros());
    littleFSReady = storageManager.begin();

    Serial.println("[Init] OLED I2C");
    OLED_Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN, 400000);
    if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        displayReady = true;
        showMessage("HeartCare", "System Booting...");
        Serial.println("[OLED] 初始化成功");
    } else {
        displayReady = false;
        Serial.println("[OLED] 初始化失敗");
    }

    Serial.println("[Init] MAX30100 I2C");
    Wire.begin(MAX30100_SDA_PIN, MAX30100_SCL_PIN, 100000);
    poxReady = pox.begin();
    if (poxReady) {
        pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);
        Serial.println("[MAX30100] 初始化成功");
    } else {
        Serial.println("[MAX30100] 初始化失敗");
    }

    dht.begin();
    Serial.println("[DHT22] 初始化完成");

    MedicationSetting medSetting = storageManager.loadMedicationSetting();
    Serial.println("[NVS] 用藥設定載入完成");
    if (medSetting.isEnabled) {
        Serial.printf("[Medication] %s @ %s\n", medSetting.name, medSetting.time);
    }

    connectWiFi();

    currentSystemState = STATE_IDLE;
    currentSafetyState = STANDBY;
    lastPeriodicReportTime = millis();

    Serial.println("====================================");
    Serial.println("HeartCare Initialization Complete");
    Serial.println("Button: GPIO27 (Short: Quiz/+1, Long: OK)");
    Serial.printf("Proximity Auto-Check: <= %ld cm for %lu ms\n", PRESENCE_THRESHOLD_CM, PRESENCE_CONFIRM_MS);
    Serial.printf("Periodic Cloud Report: every %lu ms\n", PERIODIC_REPORT_INTERVAL);
    Serial.println("====================================");
}

// ============================================================================
// 27. MAIN LOOP
// ============================================================================
void loop() {
    if (poxReady) pox.update();

    updateButton();
    updateSensors();
    updateSafetyFSM();
    updateBuzzer();
    updateStatusDisplay();
    updateWiFi();
    updatePeriodicReport();

    if (millis() - lastQueueCheckTime >= QUEUE_CHECK_INTERVAL) {
        lastQueueCheckTime = millis();
        processPendingQueue();
    }

    delay(5);
}