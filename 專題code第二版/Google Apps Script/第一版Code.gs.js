/*************************************************
 * HeartCare 系統 - Code.gs (重構版)
 * 1. 接收所有設備 JSON 數據
 * 2. 儲存 Google Sheets (完整數據)
 * 3. 獨立異常判斷 (實現獨立通知)
 * 4. 發送 LINE 通知
 *************************************************/

// Google Sheet ID
const SHEET_ID = "1YGpMl0RqhJNAxfdQ9QuZkhAOlzioX8d88uvGhBgaelQ"; 

// LINE Notify Token
const LINE_TOKEN = "你的LINE_NOTIFY_TOKEN";

// 閾值設定
const HEART_RATE_MIN = 60;
const HEART_RATE_MAX = 100;
const SPO2_WARNING = 95;
const SPO2_DANGER = 90;
const TEMP_CRITICAL_MAX = 35.0; // 新增：溫度危險閾值

/*************************************************
 * Web API Entry (核心入口)
 *************************************************/
function doPost(e) {

  try {

    const rawData = JSON.parse(e.postData.contents);

    const timestamp = new Date();

    // 1. 提取所有可能的數據欄位，使用預設值以確保腳本不會因缺失欄位而崩潰
    const data = {
      timestamp: timestamp,
      deviceId: rawData.deviceId || "ESP32_Unknown",
      heartRate: rawData.heartRate || 0,
      spo2: rawData.spo2 || 0,
      temperature: rawData.temperature !== undefined ? rawData.temperature : null,
      humidity: rawData.humidity !== undefined ? rawData.humidity : null,
      lightLevel: rawData.light_level !== undefined ? rawData.light_level : 0,
      distanceCm: rawData.distance_cm !== undefined ? rawData.distance_cm : -1,
      quizCorrect: rawData.quiz_correct || false,
      userAnswer: rawData.quiz_answer || 0,
      correctAnswer: rawData.quiz_expected || 0,
      battery: rawData.battery || "N/A",
      event_type: rawData.event_type || "SENSOR_READING" // 記錄事件類型
    };

    // 2. 儲存完整的感測器和事件數據到日誌
    saveSensorData(data);

    // 3. 啟動所有獨立異常判斷邏輯
    checkHeartRateAbnormal(data);
    checkSpo2Abnormal(data);
    checkTemperatureAbnormal(data);
    checkQuizAbnormal(data);

    // 返回成功響應
    return ContentService
      .createTextOutput(
        JSON.stringify({
          status: "success",
          message: "Data received and processed."
        })
      )
      .setMimeType(
        ContentService.MimeType.JSON
      );

  } catch (err) {
    Logger.log("Processing Error: " + err.toString());

    // 返回錯誤響應
    return ContentService
      .createTextOutput(
        JSON.stringify({
          status: "error",
          message: err.toString()
        })
      )
      .setMimeType(
        ContentService.MimeType.JSON
      );
  }
}

/*************************************************
 * 寫入 Google Sheets (完整數據日誌)
 *************************************************/
function saveSensorData(data) {
  const ss = SpreadsheetApp.openById(SHEET_ID);
  // 使用一個更通用的 Sheet 進行數據記錄
  const sheet = ss.getSheetByName("SensorDataLog");

  // 完整的欄位列表:
  // Timestamp | DeviceID | HR | SpO2 | Temp | Humidity | Light | Distance | Quiz_Status | Quiz_Ans | Quiz_Exp | Battery | Event_Type
  sheet.appendRow([
    data.timestamp,
    data.deviceId,
    data.heartRate,
    data.spo2,
    data.temperature,
    data.humidity,
    data.lightLevel,
    data.distanceCm,
    data.quizCorrect ? "PASS" : "FAIL",
    data.userAnswer,
    data.correctAnswer,
    data.battery,
    data.event_type
  ]);
}

/*************************************************
 * 獨立異常檢查模組 (實現獨立通知)
 *************************************************/

/**
 * 檢查心率異常並發送通知
 * @param {Object} data - 來自 ESP32 的完整數據
 */
function checkHeartRateAbnormal(data) {
  let message = null;

  if (data.heartRate > 0) {
    // 心率過低
    if (data.heartRate < HEART_RATE_MIN) {
      message = "心率過低 - " + data.deviceId + "\n心率：" + data.heartRate + " BPM";
    }
    // 心率過高
    else if (data.heartRate > HEART_RATE_MAX) {
      message = "心率過高 - " + data.deviceId + "\n心率：" + data.heartRate + " BPM";
    }
  }
  
  if (message) {
    const alertMessage = "【HeartCare 異常通知：心率】\n\n" + message + "\n\n時間：" + Utilities.formatDate(data.timestamp, Session.getScriptTimeZone(), "yyyy/MM/dd HH:mm:ss");
    sendLineNotify(alertMessage);
    saveAlertLog(data.timestamp, "HeartRateAnomaly: " + message);
  }
}

/**
 * 檢查血氧異常並發送通知
 * @param {Object} data - 來自 ESP32 的完整數據
 */
function checkSpo2Abnormal(data) {
  let message = null;

  if (data.spo2 > 0) {
    // 血氧危險 (Danger Zone)
    if (data.spo2 < SPO2_DANGER) {
      message = "血氧危險 - " + data.deviceId + "\nSpO2：" + data.spo2 + "%";
    }
    // 血氧偏低 (Warning Zone)
    else if (data.spo2 < SPO2_WARNING) {
      message = "血氧偏低 - " + data.deviceId + "\nSpO2：" + data.spo2 + "%";
    }
  }

  if (message) {
    const alertMessage = "【HeartCare 異常通知：SpO2】\n\n" + message + "\n\n時間：" + Utilities.formatDate(data.timestamp, Session.getScriptTimeZone(), "yyyy/MM/dd HH:mm:ss");
    sendLineNotify(alertMessage);
    saveAlertLog(data.timestamp, "SpO2Anomaly: " + message);
  }
}

/**
 * 檢查溫度異常並發送通知 (DHT22 數據)
 * @param {Object} data - 來自 ESP32 的完整數據
 */
function checkTemperatureAbnormal(data) {
  let message = null;

  if (data.temperature !== null && data.temperature > TEMP_CRITICAL_MAX) {
    message = "溫度危險 - " + data.deviceId + "\n溫度：" + data.temperature + " °C";
  }
  
  if (message) {
    const alertMessage = "【HeartCare 異常通知：環境溫度】\n\n" + message + "\n\n時間：" + Utilities.formatDate(data.timestamp, Session.getScriptTimeZone(), "yyyy/MM/dd HH:mm:ss");
    sendLineNotify(alertMessage);
    saveAlertLog(data.timestamp, "TemperatureAnomaly: " + message);
  }
}

/**
 * 檢查測驗失敗並發送通知 (行為異常)
 * @param {Object} data - 來自 ESP32 的完整數據
 */
function checkQuizAbnormal(data) {
  if (data.quizCorrect === false) {
    let message = "記憶測驗失敗 - " + data.deviceId + "\n用戶答案：" + data.userAnswer + ", 正確答案：" + data.correctAnswer;
    
    // 由於測驗失敗通常是需要關注的事件，也觸發警報
    const alertMessage = "【HeartCare 事件通知：測驗】\n\n" + message + "\n\n時間：" + Utilities.formatDate(data.timestamp, Session.getScriptTimeZone(), "yyyy/MM/dd HH:mm:ss");
    
    // 這裡可以選擇是否觸發 LINE 通知，如果只當作日誌則可以移除 sendLineNotify
    sendLineNotify(alertMessage); 
    
    saveAlertLog(data.timestamp, "QuizFailed: " + message);
  }
}

/*************************************************
 * 寫入異常紀錄
 *************************************************/
function saveAlertLog(timestamp, message) {
  const ss = SpreadsheetApp.openById(SHEET_ID);
  const sheet = ss.getSheetByName("AlertLog");
  sheet.appendRow([timestamp, message]);
}

/*************************************************
 * LINE Notify (傳輸工具)
 *************************************************/
function sendLineNotify(message) {
  const options = {
    method: "post",
    headers: {
      Authorization: "Bearer " + LINE_TOKEN
    },
    payload: {
      message: message
    }
  };

  UrlFetchApp.fetch(
    "https://notify-api.line.me/api/notify",
    options
  );


  doPost(e);
}
