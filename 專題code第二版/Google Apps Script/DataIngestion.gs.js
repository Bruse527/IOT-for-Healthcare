/**
 * @OnlyCurrentDoc
 * GAS 數據收集器模組：將原始事件或狀態紀錄寫入 Device_Data 分頁。
 * 欄位順序：Timestamp | Data_Type | Payload
 */

const DEVICE_SHEET_NAME = "Device_Data";

/**
 * 寫入一筆紀錄到 Device_Data
 * @param {string} dataType - 資料類型 (例如 SENSOR_DATA, STATE_TRANSITION, PEACE_CHECK)
 * @param {Object|string} payloadObj - 內容物件或字串
 */
function saveDeviceData(dataType, payloadObj) {
  try {
    const ss = SpreadsheetApp.openById(SHEET_ID);
    const sheet = ss.getSheetByName(DEVICE_SHEET_NAME);
    if (!sheet) {
      Logger.log("Sheet not found: " + DEVICE_SHEET_NAME);
      return false;
    }

    const payloadStr = (typeof payloadObj === "string")
      ? payloadObj
      : JSON.stringify(payloadObj || {});

    sheet.appendRow([
      new Date(),
      dataType || "UNKNOWN",
      payloadStr
    ]);
    return true;
  } catch (error) {
    Logger.log("Error in saveDeviceData: " + error.toString());
    return false;
  }
}

/**
 * [輔助函式] 格式化狀態轉換數據
 */
function createStateTransitionPayload(oldState, newState) {
  return JSON.stringify({
    type: "STATE_TRANSITION",
    deviceId: DEFAULT_DEVICE_ID,
    timestamp: new Date().toISOString(),
    oldState: oldState,
    newState: newState
  });
}

/**
 * [輔助函式] 格式化系統日誌數據
 */
function createLogPayload(logType, value, message) {
  return JSON.stringify({
    type: "SYSTEM_LOG",
    deviceId: DEFAULT_DEVICE_ID,
    timestamp: new Date().toISOString(),
    logType: logType,
    value: value,
    message: message
  });
}