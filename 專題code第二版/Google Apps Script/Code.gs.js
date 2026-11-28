/*************************************************
 * HeartCare 系統 - Code.gs (核心入口與商業邏輯)
 * 對應分頁：
 * 1. SensorDataLog
 * 2. MedicationSchedule
 * 3. MedicationLog
 * 4. AlertLog
 * 5. Device_Data (透過 DataIngestion.gs 寫入)
 *************************************************/

// =========================================
// 全域配置 
// =========================================
const SHEET_ID = "1YGpMl0RqhJNAxfdQ9QuZkhAOlzioX8d88uvGhBgaelQ";
const LINE_CHANNEL_ACCESS_TOKEN = "請替換為您重新發行的_LINE_CHANNEL_ACCESS_TOKEN";

const DEFAULT_DEVICE_ID = "HeartCare_ESP32_01";

// 異常判斷閾值 & 警報冷卻時間
const HEART_RATE_MIN = 60;
const HEART_RATE_MAX = 100;
const SPO2_WARNING = 95;
const SPO2_DANGER = 90;
const TEMP_CRITICAL_MAX = 35.0;
const ALERT_COOLDOWN_MINUTES = 10; // 同類異常 10 分鐘內只推播一次 LINE

// =========================================
// 藥品配置與服藥紀錄管理器 (MedicationConfigManager)
// =========================================
const MedicationConfigManager = {
  SCHEDULE_SHEET: "MedicationSchedule",
  LOG_SHEET: "MedicationLog",

  formatTimeCell: function(val) {
    if (val instanceof Date) {
      return Utilities.formatDate(val, "GMT+8", "HH:mm");
    }
    const str = String(val || "").trim();
    if (/^\d:\d{2}$/.test(str)) {
      return "0" + str;
    }
    return str;
  },

  generateNextId: function(data) {
    let maxNum = 0;
    for (let i = 1; i < data.length; i++) {
      const idStr = String(data[i][0] || "");
      const match = idStr.match(/^M(\d+)$/i);
      if (match) {
        const num = parseInt(match[1], 10);
        if (num > maxNum) maxNum = num;
      }
    }
    return "M" + String(maxNum + 1).padStart(3, "0");
  },

  listAllDrugs: function(includeInactive) {
    const ss = SpreadsheetApp.openById(SHEET_ID);
    const sheet = ss.getSheetByName(this.SCHEDULE_SHEET);
    if (!sheet) {
      Logger.log("Sheet not found: " + this.SCHEDULE_SHEET);
      return [];
    }

    const values = sheet.getDataRange().getValues();
    const drugs = [];

    for (let i = 1; i < values.length; i++) {
      const row = values[i];
      const id = String(row[0] || "").trim();
      const drugName = String(row[1] || "").trim();
      const activeVal = row[4];
      const isActive = (activeVal !== false && String(activeVal).toUpperCase() !== "FALSE");

      if (drugName && (includeInactive || isActive)) {
        drugs.push({
          rowIndex: i + 1,
          id: id,
          drugName: drugName,
          dosage: String(row[2] || "").trim(),
          scheduleTime: this.formatTimeCell(row[3]),
          active: isActive
        });
      }
    }
    return drugs;
  },

  addDrug: function(drugName, dosage, scheduleTime) {
    const ss = SpreadsheetApp.openById(SHEET_ID);
    const sheet = ss.getSheetByName(this.SCHEDULE_SHEET);
    if (!sheet) return null;

    const data = sheet.getDataRange().getValues();
    const newId = this.generateNextId(data);
    const normalizedTime = this.formatTimeCell(scheduleTime);

    sheet.appendRow([newId, drugName, dosage, "'" + normalizedTime, true]);

    return {
      id: newId,
      drugName: drugName,
      dosage: dosage,
      scheduleTime: normalizedTime
    };
  },

  modifyDrug: function(idOrName, newDosage, newScheduleTime) {
    const ss = SpreadsheetApp.openById(SHEET_ID);
    const sheet = ss.getSheetByName(this.SCHEDULE_SHEET);
    if (!sheet) return false;

    const data = sheet.getDataRange().getValues();
    const target = String(idOrName).trim().toLowerCase();

    for (let i = data.length - 1; i > 0; i--) {
      const rowId = String(data[i][0] || "").trim().toLowerCase();
      const rowName = String(data[i][1] || "").trim().toLowerCase();

      if (rowId === target || rowName === target) {
        const rowNum = i + 1;
        if (newDosage && newDosage !== "-") {
          sheet.getRange(rowNum, 3).setValue(newDosage);
        }
        if (newScheduleTime && newScheduleTime !== "-") {
          sheet.getRange(rowNum, 4).setValue("'" + this.formatTimeCell(newScheduleTime));
        }
        return true;
      }
    }
    return false;
  },

  stopDrug: function(idOrName) {
    const ss = SpreadsheetApp.openById(SHEET_ID);
    const sheet = ss.getSheetByName(this.SCHEDULE_SHEET);
    if (!sheet) return false;

    const data = sheet.getDataRange().getValues();
    const target = String(idOrName).trim().toLowerCase();

    for (let i = data.length - 1; i > 0; i--) {
      const rowId = String(data[i][0] || "").trim().toLowerCase();
      const rowName = String(data[i][1] || "").trim().toLowerCase();
      if (rowId === target || rowName === target) {
        sheet.getRange(i + 1, 5).setValue(false);
        return true;
      }
    }
    return false;
  },

  deleteDrug: function(idOrName) {
    const ss = SpreadsheetApp.openById(SHEET_ID);
    const sheet = ss.getSheetByName(this.SCHEDULE_SHEET);
    if (!sheet) return false;

    const data = sheet.getDataRange().getValues();
    const target = String(idOrName).trim().toLowerCase();

    for (let i = data.length - 1; i > 0; i--) {
      const rowId = String(data[i][0] || "").trim().toLowerCase();
      const rowName = String(data[i][1] || "").trim().toLowerCase();
      if (rowId === target || rowName === target) {
        sheet.deleteRow(i + 1);
        return true;
      }
    }
    return false;
  },

  logIntake: function(idOrName) {
    const drugs = this.listAllDrugs(false);
    const target = String(idOrName).trim().toLowerCase();
    const matched = drugs.find(d => d.id.toLowerCase() === target || d.drugName.toLowerCase() === target);

    if (!matched) return null;

    const ss = SpreadsheetApp.openById(SHEET_ID);
    const logSheet = ss.getSheetByName(this.LOG_SHEET);
    if (!logSheet) {
      Logger.log("Sheet not found: " + this.LOG_SHEET);
      return null;
    }

    const now = new Date();
    const dateStr = Utilities.formatDate(now, "GMT+8", "yyyy-MM-dd");

    logSheet.appendRow([
      now,
      "'" + dateStr,
      matched.id,
      matched.drugName,
      "TAKEN"
    ]);

    return matched;
  },

  getDailyIntake: function() {
    const drugs = this.listAllDrugs(false);
    const ss = SpreadsheetApp.openById(SHEET_ID);
    const logSheet = ss.getSheetByName(this.LOG_SHEET);

    const todayStr = Utilities.formatDate(new Date(), "GMT+8", "yyyy-MM-dd");
    const takenMap = {};

    if (logSheet) {
      const logData = logSheet.getDataRange().getValues();
      for (let i = 1; i < logData.length; i++) {
        const row = logData[i];
        let rowDate = row[1];
        if (rowDate instanceof Date) {
          rowDate = Utilities.formatDate(rowDate, "GMT+8", "yyyy-MM-dd");
        } else {
          rowDate = String(rowDate || "").trim();
        }

        const drugId = String(row[2] || "").trim().toLowerCase();
        const drugName = String(row[3] || "").trim().toLowerCase();
        const status = String(row[4] || "").trim().toUpperCase();

        if (rowDate === todayStr && status === "TAKEN") {
          const timeStr = (row[0] instanceof Date)
            ? Utilities.formatDate(row[0], "GMT+8", "HH:mm")
            : "";
          if (drugId) takenMap[drugId] = timeStr;
          if (drugName) takenMap[drugName] = timeStr;
        }
      }
    }

    return drugs.map(drug => {
      const intakeTime = takenMap[drug.id.toLowerCase()] || takenMap[drug.drugName.toLowerCase()] || null;
      return {
        id: drug.id,
        drugName: drug.drugName,
        dosage: drug.dosage,
        scheduledTime: drug.scheduleTime,
        isTaken: intakeTime !== null,
        intakeTime: intakeTime
      };
    });
  },

  checkScheduleMatch: function(now) {
    const drugs = this.listAllDrugs(false);
    const currentTimeStr = Utilities.formatDate(now, "GMT+8", "HH:mm");
    return drugs.filter(d => d.scheduleTime === currentTimeStr);
  }
};

// =========================================
// 核心唯一 API 入口
// =========================================
function doPost(e) {
  try {
    if (!e || !e.postData || !e.postData.contents) {
      return ContentService
        .createTextOutput(JSON.stringify({ status: "error", message: "Empty request body." }))
        .setMimeType(ContentService.MimeType.JSON);
    }

    const postData = JSON.parse(e.postData.contents);

    if (postData.events !== undefined) {
      return handleLineWebhook(postData);
    }

    const devId = postData.deviceId || postData.device_id;
    if (devId) {
      saveDeviceData(postData.event_type || postData.dataType || postData.type || "SENSOR_DATA", postData);

      if (postData.heartRate !== undefined || postData.heart_rate !== undefined || postData.event_type) {
        return handleSensorData(postData);
      }

      return ContentService
        .createTextOutput(JSON.stringify({ status: "success", message: "Device data ingested." }))
        .setMimeType(ContentService.MimeType.JSON);
    }

    return ContentService
      .createTextOutput(JSON.stringify({ status: "error", message: "Unknown payload type." }))
      .setMimeType(ContentService.MimeType.JSON);

  } catch (err) {
    Logger.log("Fatal doPost Error: " + err.toString());
    return ContentService
      .createTextOutput(JSON.stringify({ status: "error", message: err.toString() }))
      .setMimeType(ContentService.MimeType.JSON);
  }
}

// =========================================
// 1. 感測器數據處理 (SensorDataLog)
// =========================================
function normalizeSensorPayload(raw) {
  return {
    timestamp: raw.timestamp ? new Date(raw.timestamp) : new Date(),
    deviceId: raw.deviceId || raw.device_id || DEFAULT_DEVICE_ID,
    heartRate: Number(raw.heartRate !== undefined ? raw.heartRate : (raw.heart_rate || 0)),
    spo2: Number(raw.spo2 !== undefined ? raw.spo2 : 0),
    temperature: (raw.temperature === null || raw.temperature === undefined || isNaN(Number(raw.temperature)))
      ? null : Number(raw.temperature),
    humidity: (raw.humidity === null || raw.humidity === undefined || isNaN(Number(raw.humidity)))
      ? null : Number(raw.humidity),
    lightLevel: Number(raw.lightLevel !== undefined ? raw.lightLevel : (raw.light_level || 0)),
    distanceCm: Number(raw.distanceCm !== undefined ? raw.distanceCm : (raw.distance_cm !== undefined ? raw.distance_cm : -1)),
    quizCorrect: Boolean(raw.quizCorrect !== undefined ? raw.quizCorrect : raw.quiz_correct),
    userAnswer: raw.userAnswer !== undefined ? raw.userAnswer : (raw.quiz_answer !== undefined ? raw.quiz_answer : ""),
    correctAnswer: raw.correctAnswer !== undefined ? raw.correctAnswer : (raw.quiz_expected !== undefined ? raw.quiz_expected : ""),
    battery: raw.battery !== undefined ? Number(raw.battery) : 100,
    event_type: raw.event_type || "SENSOR_READING"
  };
}

function handleSensorData(rawData) {
  const data = normalizeSensorPayload(rawData);

  if (!validateSensorData(data)) {
    return ContentService
      .createTextOutput(JSON.stringify({ status: "warning", message: "Validation failed." }))
      .setMimeType(ContentService.MimeType.JSON);
  }

  // 寫入 SensorDataLog
  saveSensorData(data);

  // periodic_status 僅記錄環境與最後數值，不重複對舊心率/血氧發送異常警報
  const isPeriodic = (data.event_type === "periodic_status");
  if (!isPeriodic) {
    checkHeartRateAbnormal(data);
    checkSpo2Abnormal(data);
  }

  checkTemperatureAbnormal(data);
  checkQuizAbnormal(data);

  return ContentService
    .createTextOutput(JSON.stringify({ status: "success", message: "Sensor data processed." }))
    .setMimeType(ContentService.MimeType.JSON);
}

function saveSensorData(data) {
  const ss = SpreadsheetApp.openById(SHEET_ID);
  const sheet = ss.getSheetByName("SensorDataLog");
  if (!sheet) return;

  const isQuizEvent = (data.event_type === "memory_quiz" || data.event_type === "safety_check_completed");
  const quizStatus = isQuizEvent ? (data.quizCorrect ? "PASS" : "FAIL") : "NONE";

  sheet.appendRow([
    data.timestamp,
    data.deviceId,
    data.heartRate,
    data.spo2,
    data.temperature !== null ? data.temperature : "",
    data.humidity !== null ? data.humidity : "",
    data.lightLevel,
    data.distanceCm,
    quizStatus,
    data.userAnswer,
    data.correctAnswer,
    data.battery,
    data.event_type
  ]);
}

// =========================================
// 2. 異常冷卻保護與檢查 (AlertLog)
// =========================================
/**
 * 檢查特定異常類型是否已過冷卻時間 (預設 10 分鐘)
 */
function canTriggerAlert(alertType) {
  const props = PropertiesService.getScriptProperties();
  const key = "ALERT_CD_" + alertType;
  const lastMs = Number(props.getProperty(key) || "0");
  const nowMs = Date.now();

  if (nowMs - lastMs < ALERT_COOLDOWN_MINUTES * 60 * 1000) {
    Logger.log(`Alert [${alertType}] skipped due to ${ALERT_COOLDOWN_MINUTES}m cooldown.`);
    return false;
  }

  props.setProperty(key, String(nowMs));
  return true;
}

function validateSensorData(data) {
  const errors = [];
  if (data.heartRate < 0 || data.heartRate > 250) errors.push("HR 範圍異常");
  if (data.spo2 < 0 || data.spo2 > 100) errors.push("SpO2 範圍異常");
  if (data.temperature !== null && (data.temperature < -10 || data.temperature > 60)) errors.push("Temp 範圍異常");
  if (data.humidity !== null && (data.humidity < 0 || data.humidity > 100)) errors.push("Humidity 範圍異常");

  if (errors.length > 0) {
    const msg = errors.join(", ");
    if (canTriggerAlert("SENSOR_INVALID")) {
      const timeStr = Utilities.formatDate(data.timestamp, "GMT+8", "yyyy/MM/dd HH:mm:ss");
      sendLineAlert(`【⚠️ 感測器量測無效】\n設備：${data.deviceId}\n原因：${msg}\n時間：${timeStr}`);
      saveAlertLog(data.timestamp, data.deviceId, "SENSOR_INVALID", msg);
    }
    return false;
  }
  return true;
}

function checkHeartRateAbnormal(data) {
  if (data.heartRate <= 0) return;

  let msg = null;
  if (data.heartRate < HEART_RATE_MIN) {
    msg = `心率過低：${data.heartRate} BPM`;
  } else if (data.heartRate > HEART_RATE_MAX) {
    msg = `心率過高：${data.heartRate} BPM`;
  }

  if (msg && canTriggerAlert("HIGH_LOW_HR")) {
    const timeStr = Utilities.formatDate(data.timestamp, "GMT+8", "yyyy/MM/dd HH:mm:ss");
    sendLineAlert(`【🚨 HeartCare 心率異常】\n設備：${data.deviceId}\n${msg}\n時間：${timeStr}`);
    saveAlertLog(data.timestamp, data.deviceId, "HIGH_LOW_HR", msg);
  }
}

function checkSpo2Abnormal(data) {
  if (data.spo2 <= 0) return;

  let msg = null;
  let type = "LOW_SPO2";
  if (data.spo2 < SPO2_DANGER) {
    msg = `血氧危險：${data.spo2}%`;
    type = "DANGER_SPO2";
  } else if (data.spo2 < SPO2_WARNING) {
    msg = `血氧偏低：${data.spo2}%`;
  }

  if (msg && canTriggerAlert("SPO2_ABNORMAL")) {
    const timeStr = Utilities.formatDate(data.timestamp, "GMT+8", "yyyy/MM/dd HH:mm:ss");
    sendLineAlert(`【🚨 HeartCare 血氧異常】\n設備：${data.deviceId}\n${msg}\n時間：${timeStr}`);
    saveAlertLog(data.timestamp, data.deviceId, type, msg);
  }
}

function checkTemperatureAbnormal(data) {
  if (data.temperature !== null && data.temperature > TEMP_CRITICAL_MAX) {
    if (canTriggerAlert("HIGH_TEMP")) {
      const msg = `環境溫度過高：${data.temperature.toFixed(1)} °C`;
      const timeStr = Utilities.formatDate(data.timestamp, "GMT+8", "yyyy/MM/dd HH:mm:ss");
      sendLineAlert(`【🔥 HeartCare 溫度異常】\n設備：${data.deviceId}\n${msg}\n時間：${timeStr}`);
      saveAlertLog(data.timestamp, data.deviceId, "HIGH_TEMP", msg);
    }
  }
}

function checkQuizAbnormal(data) {
  if (data.event_type === "memory_quiz" && data.quizCorrect === false) {
    const msg = `記憶測驗未通過 (作答: ${data.userAnswer} / 正確: ${data.correctAnswer})`;
    const timeStr = Utilities.formatDate(data.timestamp, "GMT+8", "yyyy/MM/dd HH:mm:ss");
    sendLineAlert(`【⚠️ HeartCare 測驗事件】\n設備：${data.deviceId}\n${msg}\n時間：${timeStr}`);
    saveAlertLog(data.timestamp, data.deviceId, "QUIZ_FAILED", msg);
  } else if (data.event_type === "no_response") {
    const msg = "使用者安全確認超時未回應";
    const timeStr = Utilities.formatDate(data.timestamp, "GMT+8", "yyyy/MM/dd HH:mm:ss");
    sendLineAlert(`【🚨 HeartCare 緊急通知】\n設備：${data.deviceId}\n${msg}\n時間：${timeStr}`);
    saveAlertLog(data.timestamp, data.deviceId, "NO_RESPONSE", msg);
  }
}

function checkScheduledMedications() {
  const now = new Date();
  const matchedDrugs = MedicationConfigManager.checkScheduleMatch(now);
  if (matchedDrugs.length === 0) return;

  const props = PropertiesService.getScriptProperties();
  const todayKey = Utilities.formatDate(now, "GMT+8", "yyyy-MM-dd_HH:mm");

  matchedDrugs.forEach(drug => {
    const sentKey = `MED_REMIND_${drug.id}_${todayKey}`;
    if (props.getProperty(sentKey)) return;

    const msg = `【💊 用藥時間提醒】\n藥品：${drug.drugName} (${drug.id})\n劑量：${drug.dosage}\n排程時間：${drug.scheduleTime}\n\n服藥後可輸入：/log_medication "${drug.drugName}"`;
    sendLineAlert(msg);
    saveAlertLog(now, "SYSTEM", "MED_REMINDER", `提醒服用 ${drug.drugName} (${drug.dosage})`);
    props.setProperty(sentKey, "1");
  });
}

function dailyCheckinMonitor() {
  const latestData = getLatestSensorData(null);
  const todayStr = Utilities.formatDate(new Date(), "GMT+8", "yyyy-MM-dd");

  let hasPassedToday = false;
  if (latestData) {
    const ss = SpreadsheetApp.openById(SHEET_ID);
    const sheet = ss.getSheetByName("SensorDataLog");
    const data = sheet.getDataRange().getValues();
    for (let i = data.length - 1; i > 0; i--) {
      const ts = new Date(data[i][0]);
      if (Utilities.formatDate(ts, "GMT+8", "yyyy-MM-dd") === todayStr && data[i][8] === "PASS") {
        hasPassedToday = true;
        break;
      }
    }
  }

  if (!hasPassedToday) {
    const msg = `【🚨 平安確認提醒】\n今日 (${todayStr}) 尚未收到成功的認知測驗平安確認紀錄，請關心長輩狀況。`;
    sendLineAlert(msg);
    saveAlertLog(new Date(), DEFAULT_DEVICE_ID, "CHECKIN_FAIL", "今日未完成平安確認");
  }
}

// =========================================
// 3. 查詢與寫入輔助函數
// =========================================
function getLatestSensorData(deviceId) {
  const ss = SpreadsheetApp.openById(SHEET_ID);
  const sheet = ss.getSheetByName("SensorDataLog");
  if (!sheet) return null;

  const data = sheet.getDataRange().getValues();
  if (data.length <= 1) return null;

  for (let i = data.length - 1; i >= 1; i--) {
    const row = data[i];
    const rowDeviceId = String(row[1] || "");
    if (!deviceId || rowDeviceId === deviceId) {
      return {
        timestamp: row[0],
        deviceId: rowDeviceId,
        heartRate: row[2],
        spo2: row[3],
        temperature: row[4],
        humidity: row[5],
        lightLevel: row[6],
        distanceCm: row[7],
        quizStatus: row[8],
        quizCorrect: row[8] === "PASS",
        userAnswer: row[9],
        correctAnswer: row[10],
        battery: row[11],
        event_type: row[12]
      };
    }
  }
  return null;
}

function getSensorDataByTimeRange(deviceId, startDate, endDate) {
  const ss = SpreadsheetApp.openById(SHEET_ID);
  const sheet = ss.getSheetByName("SensorDataLog");
  if (!sheet) return [];

  const data = sheet.getDataRange().getValues();
  if (data.length <= 1) return [];

  const results = [];
  for (let i = 1; i < data.length; i++) {
    const row = data[i];
    const rowDeviceId = String(row[1] || "");
    if (deviceId && rowDeviceId !== deviceId) continue;

    const ts = new Date(row[0]);
    if (ts >= startDate && ts <= endDate) {
      results.push({
        timestamp: ts,
        deviceId: rowDeviceId,
        heartRate: Number(row[2] || 0),
        spo2: Number(row[3] || 0),
        temperature: row[4] !== "" ? Number(row[4]) : null,
        humidity: row[5] !== "" ? Number(row[5]) : null,
        distanceCm: Number(row[7] || -1),
        quizCorrect: row[8] === "PASS",
        battery: Number(row[11] || 0)
      });
    }
  }
  return results.sort((a, b) => a.timestamp.getTime() - b.timestamp.getTime());
}

function saveAlertLog(timestamp, deviceId, alertType, message) {
  const ss = SpreadsheetApp.openById(SHEET_ID);
  const sheet = ss.getSheetByName("AlertLog");
  if (!sheet) return;
  sheet.appendRow([timestamp || new Date(), deviceId || "SYSTEM", alertType || "INFO", message]);
}

function sendLineAlert(messageText) {
  const userId = PropertiesService.getScriptProperties().getProperty("LINE_USER_ID");
  const url = userId
    ? "https://api.line.me/v2/bot/message/push"
    : "https://api.line.me/v2/bot/message/broadcast";

  const payload = userId
    ? { to: userId, messages: [{ type: "text", text: messageText }] }
    : { messages: [{ type: "text", text: messageText }] };

  const options = {
    method: "post",
    contentType: "application/json",
    headers: {
      Authorization: "Bearer " + LINE_CHANNEL_ACCESS_TOKEN
    },
    payload: JSON.stringify(payload),
    muteHttpExceptions: true
  };

  try {
    const res = UrlFetchApp.fetch(url, options);
    Logger.log("LINE Alert Response: " + res.getContentText());
  } catch (e) {
    Logger.log("LINE Alert Error: " + e.toString());
  }
}

// =========================================
// 4. 測試與清理工具
// =========================================
function clearTestData() {
  const ss = SpreadsheetApp.openById(SHEET_ID);
  const targetSheets = ["SensorDataLog", "AlertLog", "MedicationLog", "Device_Data"];

  targetSheets.forEach(name => {
    const sheet = ss.getSheetByName(name);
    if (sheet) {
      const lastRow = sheet.getLastRow();
      if (lastRow > 1) {
        sheet.getRange(2, 1, lastRow - 1, sheet.getLastColumn()).clearContent();
        Logger.log(name + " 已清空。");
      }
    }
  });
}

function testSensorData() {
  const fake = {
    deviceId: DEFAULT_DEVICE_ID,
    heartRate: 115,
    spo2: 92,
    temperature: 36.2,
    humidity: 55,
    lightLevel: 500,
    distanceCm: 25,
    quizCorrect: false,
    userAnswer: 1,
    correctAnswer: 2,
    battery: 88,
    event_type: "memory_quiz"
  };
  handleSensorData(fake);
}