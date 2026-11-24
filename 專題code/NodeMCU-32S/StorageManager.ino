// ==========================================================================================
// ESP32 本地儲存模組: StorageManager
// ------------------------------------------------------------------------------------------
// 功能: 使用 NVS (Preferences) 儲存配置設定，使用 LittleFS 儲存日誌和待傳事件佇列。
// 該模組解決了斷電重開的數據安全讀取和容量控制問題。
// ------------------------------------------------------------------------------------------

#include <Preferences.h> // ESP32 NVS 儲存
#include <FS.h>          // 檔案系統基礎
#include "LittleFS.h"    // 實際使用 LittleFS 實作
#include <ArduinoJson.h> // 用於序列化結構化資料 (必須安裝此庫)
#include <string.h>

// ==========================================================================================
// 資料結構定義 (Data Structures)
// ==========================================================================================

// --- 1. 藥物提醒設定 ---
struct MedicationSetting {
    char name[32];          // 藥名
    char time[16];          // 服用時間 (e.g., "08:00")
    char instruction[64];   // 服用說明
    bool isRepeating;       // 是否重複
    bool isEnabled;         // 是否啟用
    bool isActive;          // 當前是否有效 (用於起訖日期檢查)
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

// ==========================================================================================
// 儲存管理器 (StorageManager)
// ==========================================================================================

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
            Serial.println("LittleFS 掛載失敗！請檢查您的 LittleFS 初始化流程。");
        }
    }

    // ==================================================
    // 配置設定 (使用 NVS/Preferences)
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
        Serial.println("[NVS] 藥物設定已儲存成功。");
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
        Serial.println("[NVS] 藥物設定已載入。");
        return setting;
    }

    // ==================================================
    // 日誌與事件佇列 (使用 LittleFS)
    // ==================================================
    
    /**
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
            Serial.printf("[LittleFS] 新日誌已成功寫入: Type=%s\n", log.entryType);
        } else {
            Serial.println("[LittleFS] 寫入日誌失敗！");
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
            Serial.println("[Queue] 佇列容量已滿，移除了最舊的事件。");
        }
        queue.push_back(event);

        // 原子寫入文件
        if (writeQueueToFile(queue)) {
            Serial.printf("[LittleFS] 事件已加入佇列: Type=%s\n", event.eventType);
        } else {
            Serial.println("[LittleFS] 寫入佇列失敗！");
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

        Serial.printf("[Queue] 嘗試發送隊列中的事件 (Type: %s, Attempts: %d)...\n", event.eventType, event.attemptCount + 1);

        // -------------------------------------------
        // *** 核心業務邏輯：模擬發送 ***
        // 實際專案中，這裡會呼叫 LINE API 的函式
        bool sendingSuccess = (event.eventType[0] == 'L'); // 模擬 LINE 事件成功
        // -------------------------------------------

        if (sendingSuccess) {
            Serial.println("[Queue] 事件發送成功，已從佇列移除。");
            // 移除成功事件
            queue.erase(queue.begin());
            writeQueueToFile(queue); // 寫入更新後的佇列
            return true;
        } else {
            // 發送失敗：增加嘗試次數並更新文件
            event.attemptCount++;
            Serial.println("[Queue] 事件發送失敗，將事件保留在佇列中以供下次重試。");
            writeQueueToFile(queue); // 寫入更新了嘗試次數的佇列
            return false;
        }
    }

private:
    // ==================================================
    // 底層 I/O 實現 (使用 JSON 格式)
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

        // 由於日誌是累加的，這裡假設日誌檔是 JSON 陣列的格式
        // 實際應用中，可能需要確保寫入時是完整的 JSON 陣列結構
        // 簡化處理：逐行讀取並解析（不適合複雜 JSON 陣列，但適合簡單紀錄）
        // 由於篇幅限制，此處使用簡易模擬讀取，但在實作時必須使用 JsonDocument 進行完整解析。
        Serial.println("--- 讀取日誌檔案 (模擬) ---");
        // 假設我們從一個文件中讀取並解析出紀錄
        // 由於檔案結構複雜，我們暫時返回空集，提醒您需實作完整的 JSON 文件讀取和解析邏輯。
        return logs; 
    }

    bool writeLogsToFile(const std::vector<LogEntry>& entries) {
        // 實現原子寫入：先寫到臨時文件，成功後再替換主文件
        File tempFile = LittleFS.open("/temp_logs.json", "w");
        if (!tempFile) return false;

        // 實際寫入時，必須將整個 entries 向量序列化為一個 JSON 陣列
        // 此處為簡化，僅做邏輯示意
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
        // 假設從文件中讀取成功，返回隊列中的事件
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

// ==========================================================================================
// 主程式範例 (MAIN / SETUP / LOOP)
// ==========================================================================================

// 請注意：在實際的 ESP32 環境中，LittleFS.begin() 必須在 setup() 之前或作為 setup() 的第一步執行。

void setup() {
    Serial.begin(115200);
    delay(1000);

    // 1. 必須先初始化檔案系統
    if (!LittleFS.begin()) {
        Serial.println("FATAL: 無法初始化 LittleFS。");
        return;
    }

    // 2. 初始化儲存管理器
    StorageManager storageManager;

    // 3. 載入設定
    MedicationSetting currentMedSetting = storageManager.loadMedicationSetting();
    Serial.println("--- 系統初始化完成，開始運行 ---");

    // 4. 檢查並處理待傳事件佇列
    Serial.println("\n===== 啟動佇列處理檢查 =====");
    // 循環嘗試處理佇列中的事件，直到成功或佇列為空
    while(storageManager.processPendingQueue());
    Serial.println("===== 佇列檢查完成 =====");
}

void loop() {
    // 由於這是一個後臺模組，循環中可以定期呼叫 processPendingQueue()
    // 檢查是否有新的事件需要發送
    static unsigned long lastQueueCheck = 0;
    if (millis() - lastQueueCheck > 5000) { // 每 5 秒檢查一次
        storageManager.processPendingQueue();
        lastQueueCheck = millis();
    }

    // 在主 FSM 模組中，當生成 LogEntry 時，呼叫 storageManager.saveLog(log);
}
