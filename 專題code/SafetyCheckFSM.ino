// ==========================================================================================
// C++ 狀態機模組: 每日平安確認與圖案記憶測驗
// ------------------------------------------------------------------------------------------
// 該模組使用有限狀態機 (FSM) 模式來管理整個測驗流程。
// 假設外部已定義底層硬體函式 (例如：readButton(), displayPattern(), readVitals())
// ==========================================================================================

#include <iostream>
#include <vector>
#include <chrono>
#include <ctime>
#include <cmath>

// --- [結構化事件資料] ---
struct QuizResult {
    std::string status;     // 最終狀態: "Completed", "MeasurementFailed", "NoResponse"
    float accuracy;         // 答題正確率 (0.0 - 1.0)
    long reactionTime_ms;   // 反應時間 (毫秒)
    std::time_t timestamp;  // 事件發生時間戳記
};

// --- [狀態定義] ---
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

class SafetyCheckModule {
private:
    State currentState;
    QuizResult currentResult;
    
    // --- 計時與計數器 ---
    long stateStartTime;       // 記錄進入當前狀態的時間 (ms)
    int patternLength;         // 圖案序列的長度
    std::vector<int> requiredPattern; // 記憶的正確圖案序列
    int currentPatternIndex;   // 當前顯示的圖案索引
    int userPressCount;        // 用戶按鈕連擊次數
    int correctSequenceCount;  // 正確完成的序列次數
    long sequenceStartTime;    // 用戶開始互動的計時點
    
    // --- 超時設定 (關鍵參數) ---
    const long ALERT_TIMEOUT_MS = 10000; // 提醒狀態超時 (10秒)
    const long PATTERN_RESPONSE_TIMEOUT_MS = 20000; // 圖案展示狀態超時 (20秒)
    const long MEASUREMENT_TIMEOUT_MS = 15000; // 生理量測狀態超時 (15秒)


public:
    SafetyCheckModule() : 
        currentState(STANDBY), 
        patternLength(5), // 預設圖案長度
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
        std::cout << "--- SafetyCheckModule 初始化完成 (待機) ---" << std::endl;
    }

    // ==================================================
    // 外部介面 (Mockup/Placeholder for Hardware Interaction)
    // 實際專案中，這些函式會呼叫您的 LED/Button/Sensor 驅動程式
    // ==================================================

    // 假設的按鈕輸入函數
    bool readButton() {
        // 這裡替換為實際的按鈕讀取邏輯 (e.g., digitalRead(BUTTON_PIN))
        // 為了測試，我們假設外部調用者會手動控制計時
        return false; 
    }

    // 假設的圖案展示函數
    void displayPattern(int pattern_id) {
        // 這裡替換為實際的圖案顯示邏輯 (e.g., LED/OLED 改變圖案)
        std::cout << ">> [Display] 正在展示記憶圖案 ID: " << pattern_id << std::endl;
    }

    // 假設的生理量測函數 (回傳 true 表示成功，false 表示失敗)
    bool readVitals(float& hr, float& spo2) {
        // 這裡替換為 MAX30100 的讀取邏輯
        // 模擬量測失敗 (例如：手指移開)
        static int counter = 0;
        counter++;
        if (counter < 3) { // 前兩次模擬成功，第三次模擬失敗
            hr = 75.0;
            spo2 = 98.0;
            return true; 
        } else {
            std::cout << ">> [Sensor] 模擬量測失敗！" << std::endl;
            return false; 
        }
    }

    // ==================================================
    // FSM 主更新函數 (核心邏輯)
    // ==================================================

    // 每次循環呼叫此函數
    void update() {
        // 1. 狀態計時器重置
        if (currentState != STANDBY) {
            // 檢查超時條件 (State Timeout Check)
            if (millis() - stateStartTime > getTimeoutMS(currentState)) {
                // 如果進入狀態後超時，則進入「尚未回應」狀態
                if (currentState != MEASUREMENT_FAILED) {
                    transitionTo(NO_RESPONSE);
                }
                return; // 停止當前狀態的處理
            }
        }

        // 2. 根據當前狀態執行邏輯
        switch (currentState) {
            case STANDBY:
                handleStandby();
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
                // 終端狀態，不執行任何操作，等待重置
                break;
        }
    }
    
    // 啟動測驗流程
    void startTest() {
        transitionTo(ALERTING);
    }
    
    // 獲取當前結果
    QuizResult getResult() const {
        return currentResult;
    }

private:
    // --- 狀態轉移函數 ---
    void transitionTo(State newState) {
        if (currentState == newState) return; // 避免重複轉移

        std::cout << "\n================================================" << std::endl;
        std::cout << "狀態轉移: " << stateToString(currentState) << " -> " << stateToString(newState) << std::endl;
        std::cout << "================================================" << std::endl;
        
        currentState = newState;
        stateStartTime = millis(); // 重置狀態計時器

        // 狀態特定初始化
        switch (newState) {
            case ALERTING:
                // 啟動提醒邏輯
                break;
            case PATTERN_DISPLAY:
                // 準備記憶圖案
                setupPattern();
                break;
            case WAITING_INPUT:
                // 記錄用戶開始互動時間
                sequenceStartTime = millis(); 
                break;
            case VITAL_MEASUREMENT:
                // 準備量測
                break;
            case COMPLETED:
                // 整理最終數據
                finalizeResult();
                break;
            case MEASUREMENT_FAILED:
                // 失敗處理
                currentResult.status = "MeasurementFailed";
                break;
            case NO_RESPONSE:
                // 超時處理
                currentResult.status = "NoResponse";
                break;
            case STANDBY:
                // 重置所有計數器
                userPressCount = 0;
                correctSequenceCount = 0;
                break;
        }
    }

    // --- 狀態邏輯處理函式 ---

    void handleStandby() {
        // 待機狀態：等待外部觸發 startTest()
        // 可以在此處循環讀取按鈕，用於自啟動
    }

    void handleAlerting() {
        // 提醒中狀態：可能閃爍 LED 或播放聲音
        // 此處主要監控 ALERT_TIMEOUT_MS
        // 若超過時間，會自動轉移到 NO_RESPONSE
        std::cout << ">>> [State Alerting] 系統正在提醒使用者進行檢查..." << std::endl;
    }

    void handlePatternDisplay() {
        // 圖案序列展示狀態
        // 逐步展示 requiredPattern 的每個圖案
        if (currentPatternIndex < patternLength) {
            displayPattern(requiredPattern[currentPatternIndex]);
            currentPatternIndex++;
        } else {
            // 序列展示完畢，轉入等待輸入狀態
            transitionTo(WAITING_INPUT);
        }
    }

    void handleWaitingInput() {
        // 按鈕連擊計次與停頓結算狀態
        
        // 1. 處理按鈕輸入
        if (readButton()) {
            userPressCount++;
            std::cout << ">>> [Input] 按鈕已按下。計數: " << userPressCount << std::endl;

            // 2. 核心驗證邏輯 (簡化模擬)
            // 假設我們正在檢查用戶是否按下了正確的順序
            // 實際中，這裡會對每個按鍵按下進行比對 (e.g., userPressCount == patternLength)
            if (userPressCount >= patternLength) { 
                // 假設使用者成功完成了序列
                correctSequenceCount++;
                std::cout << ">>> [Input] 記憶序列驗證成功！" << std::endl;
                
                // 轉移到生理量測，並重置計數器
                userPressCount = 0;
                transitionTo(VITAL_MEASUREMENT);
            }
        }
        // 3. 狀態超時檢查會在 update() 中自動處理轉移到 NO_RESPONSE
    }

    void handleVitalMeasurement() {
        // 生理量測狀態
        float hr, spo2;
        bool success = readVitals(hr, spo2);

        if (success) {
            // 量測成功：進入完成狀態
            std::cout << ">>> [Sensor] 生理量測數據獲取成功。HR: " << hr << ", SpO2: " << spo2 << std::endl;
            // 數據儲存和後續處理...
            transitionTo(COMPLETED);
        } else {
            // 量測失敗：進入量測失敗狀態
            transitionTo(MEASUREMENT_FAILED);
        }
    }

    // ==================================================
    // 輔助函式
    // ==================================================

    void setupPattern() {
        // 隨機生成一個記憶圖案序列 (模擬)
        // 在實際專案中，這裡會根據記憶測驗規則生成圖案
        requiredPattern = {1, 3, 2, 4, 1}; 
        currentPatternIndex = 0;
        correctSequenceCount = 0;
    }

    long getTimeoutMS(State s) {
        switch (s) {
            case ALERTING: return ALERT_TIMEOUT_MS;
            case PATTERN_DISPLAY: return PATTERN_RESPONSE_TIMEOUT_MS;
            case WAITING_INPUT: return PATTERN_RESPONSE_TIMEOUT_MS; // 使用相同的計時器
            case VITAL_MEASUREMENT: return MEASUREMENT_TIMEOUT_MS;
            default: return 0; // 終端狀態或待機不計時
        }
    }
    
    void finalizeResult() {
        // 計算最終結果
        long timeTaken = millis() - stateStartTime;
        
        // 1. 計算正確性
        currentResult.accuracy = (double)correctSequenceCount / patternLength; 

        // 2. 計算反應時間 (基於整個測驗流程)
        // 這裡假設整個測驗時間就是從開始到完成的時間
        // 注意：在真實應用中，反應時間可能特指 'PATTERN_DISPLAY' 到 'WAITING_INPUT' 的間隔
        currentResult.reactionTime_ms = timeTaken; 

        // 3. 輸出結構化資料 (供儲存/LINE通知)
        std::cout << "\n==================================================" << std::endl;
        std::cout << "[結果摘要]: 測驗已完成!" << std::endl;
        std::cout << "   - 最終狀態: " << currentResult.status << std::endl;
        std::cout << "   - 正確率: " << (currentResult.accuracy * 100.0) << "%" << std::endl;
        std::cout << "   - 反應時間: " << currentResult.reactionTime_ms << " ms" << std::endl;
        std::cout << "==================================================" << std::endl;
        
        // **這就是您的結構化事件資料，可以被外部系統讀取並發送到 LINE**
        // std::string json_data = buildJson(currentResult);
    }

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

// ==========================================================================================
// 模擬運行 (Simulation)
// ==========================================================================================

void runSimulation() {
    SafetyCheckModule checker;
    
    // 模擬外部事件和循環執行
    // 1. 啟動測試
    checker.startTest();

    // 2. 模擬主要循環 (例如：在主程式的 loop() 中)
    // 這裡我們只運行少量循環來演示狀態轉移
    for (int i = 0; i < 40; ++i) {
        // 模擬時間推進
        // 如果這是硬體環境，這裡就是實際的 loop()
        
        checker.update();

        // 模擬外部按鈕點擊 (假設在 WAITING_INPUT 狀態下發生)
        // 由於 FSM 邏輯中的 readButton() 是 mock，我們模擬在第 20 輪進入 WAITING_INPUT 後，模擬按鍵按鍵 (假設 patternLength=5)
        // 實際專案中，您會根據外部事件來驅動 FSM 的轉移
    }
}

// 假設在 main 函式中調用
int main() {
    runSimulation();
    return 0;
}

