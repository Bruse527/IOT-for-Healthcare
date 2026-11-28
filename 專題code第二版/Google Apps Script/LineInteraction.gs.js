/**
 * @OnlyCurrentDoc
 * GAS LINE 交互模組：處理 LINE Webhook 指令與關鍵字回覆。
 */

function replyToLine(replyToken, messageText) {
  const url = "https://api.line.me/v2/bot/message/reply";
  const payload = {
    replyToken: replyToken,
    messages: [{ type: "text", text: messageText }]
  };

  const options = {
    method: "post",
    contentType: "application/json",
    headers: { Authorization: "Bearer " + LINE_CHANNEL_ACCESS_TOKEN },
    payload: JSON.stringify(payload),
    muteHttpExceptions: true
  };

  try {
    const response = UrlFetchApp.fetch(url, options);
    Logger.log("LINE Reply Response: " + response.getContentText());
  } catch (e) {
    Logger.log("Error sending message to LINE: " + e.toString());
  }
}

/**
 * 解析斜線指令與引號參數 (修正了原本 \s 的 Regex Bug)
 */
function parseCommand(messageText) {
  const trimmedMsg = messageText.trim();
  if (!trimmedMsg.startsWith("/")) return null;

  const partsString = trimmedMsg.substring(1).trim();
  const commandMatch = partsString.match(/^([a-zA-Z0-9_]+)(.*)/);
  if (!commandMatch) return null;

  const command = commandMatch[1].toLowerCase();
  const rawArgs = commandMatch[2].trim();
  const args = [];

  const argMatches = rawArgs.match(/"([^"]*)"|(\S+)/g) || [];
  for (const arg of argMatches) {
    if (arg.startsWith('"') && arg.endsWith('"')) {
      args.push(arg.slice(1, -1));
    } else if (arg) {
      args.push(arg);
    }
  }
  return { command: command, args: args };
}

/**
 * 接收來自 Code.gs doPost 分流過來的 LINE Webhook 事件
 */
function handleLineWebhook(json) {
  const events = json.events || [];

  events.forEach(event => {
    // 記憶最後互動的 UserID，供警報推播使用
    if (event.source && event.source.userId) {
      PropertiesService.getScriptProperties().setProperty("LINE_USER_ID", event.source.userId);
    }

    const replyToken = event.replyToken;
    if (!replyToken) return;

    if (event.type === "message" && event.message.type === "text") {
      const messageText = event.message.text;
      const keyword = messageText.trim().toLowerCase();
      const parsed = parseCommand(messageText);

      if (parsed) {
        switch (parsed.command) {
          case "add_drug":
            handleAddMedication(replyToken, parsed.args);
            break;
          case "list_drugs":
            handleListMedications(replyToken);
            break;
          case "modify_drug":
            handleModifyMedication(replyToken, parsed.args);
            break;
          case "delete_drug":
            handleDeleteMedication(replyToken, parsed.args, false);
            break;
          case "stop_drug":
            handleDeleteMedication(replyToken, parsed.args, true);
            break;
          case "log_medication":
            handleLogMedication(replyToken, parsed.args);
            break;
          case "daily_intake":
          case "today_meds":
            handleGetDailyIntake(replyToken);
            break;
          case "check_status":
          case "get_latest_data":
            handleGetLatestStatus(replyToken);
            break;
          case "history_hr":
          case "get_history":
            handleGetHistoricalHR(replyToken, parsed.args);
            break;
          case "peace_check":
            handleCheckPeace(replyToken);
            break;
          default:
            replyToLine(replyToken, `🚫 未知指令：/${parsed.command}\n輸入「說明」可查看可用功能。`);
        }
      } else {
        if (keyword.includes("健康狀態") || keyword.includes("最新數據")) {
          handleGetLatestStatus(replyToken);
        } else if (keyword.includes("健康紀錄") || keyword.includes("歷史趨勢")) {
          handleGetHistoricalHR(replyToken, []);
        } else if (keyword.includes("用藥清單") || keyword.includes("藥品列表")) {
          handleListMedications(replyToken);
        } else if (keyword.includes("用藥管理") || keyword.includes("新增藥品") || keyword.includes("修改藥品")) {
          handleAddMedication(replyToken, null);
        } else if (keyword.includes("今日提醒") || keyword.includes("用藥排程") || keyword.includes("服藥紀錄")) {
          handleGetDailyIntake(replyToken);
        } else if (keyword.includes("平安確認") || keyword.includes("狀態")) {
          handleCheckPeace(replyToken);
        } else {
          replyToLine(
            replyToken,
            "👋 您好！請輸入關鍵字或指令使用服務：\n" +
            "• 健康狀態 (查詢最新感測數據)\n" +
            "• 健康紀錄 (查看近 7 天心率/血氧趨勢)\n" +
            "• 今日提醒 (查看今日服藥狀態)\n" +
            "• 用藥清單 (查看目前排程藥品)\n" +
            "• 用藥管理 (查看新增/修改/服藥指令)\n" +
            "• 平安確認 (查看今日認知測驗狀態)"
          );
        }
      }
    }
  });

  return ContentService.createTextOutput("OK").setMimeType(ContentService.MimeType.TEXT);
}

// ===================================================
// 真實業務邏輯實現
// ===================================================

function handleGetLatestStatus(replyToken) {
  try {
    const latest = getLatestSensorData(null);
    if (!latest) {
      replyToLine(replyToken, "⚠️ 目前 SensorDataLog 尚無感測器數據。");
      return;
    }

    const timeStr = Utilities.formatDate(new Date(latest.timestamp), "GMT+8", "yyyy/MM/dd HH:mm:ss");
    const tempStr = (latest.temperature !== "" && latest.temperature !== null) ? `${latest.temperature}°C` : "N/A";
    const humStr = (latest.humidity !== "" && latest.humidity !== null) ? `${latest.humidity}%` : "N/A";

    const message =
      "🌡️【最新健康狀態】\n" +
      `🕒 更新時間：${timeStr}\n` +
      `📟 設備：${latest.deviceId}\n` +
      `❤️ 心率 (HR)：${latest.heartRate || "N/A"} BPM\n` +
      `🩸 血氧 (SpO2)：${latest.spo2 || "N/A"}%\n` +
      `🌡️ 溫度：${tempStr} | 💧 濕度：${humStr}\n` +
      `📏 距離：${latest.distanceCm} cm | 🔋 電池：${latest.battery}%\n` +
      `🤖 事件類型：${latest.event_type}`;

    replyToLine(replyToken, message);
  } catch (e) {
    replyToLine(replyToken, "⚠️ 數據查詢失敗：" + e.toString());
  }
}

function handleCheckPeace(replyToken) {
  try {
    const ss = SpreadsheetApp.openById(SHEET_ID);
    const sheet = ss.getSheetByName("SensorDataLog");
    const todayStr = Utilities.formatDate(new Date(), "GMT+8", "yyyy-MM-dd");

    let lastPassTime = null;
    if (sheet) {
      const data = sheet.getDataRange().getValues();
      for (let i = data.length - 1; i > 0; i--) {
        const ts = new Date(data[i][0]);
        if (Utilities.formatDate(ts, "GMT+8", "yyyy-MM-dd") === todayStr && data[i][8] === "PASS") {
          lastPassTime = Utilities.formatDate(ts, "GMT+8", "HH:mm:ss");
          break;
        }
      }
    }

    saveDeviceData("PEACE_CHECK", { source: "LINE", checkedAt: new Date().toISOString() });

    if (lastPassTime) {
      replyToLine(replyToken, `✅【今日平安確認】\n\n今日 (${todayStr}) 已於 ${lastPassTime} 通過認知測驗平安確認！`);
    } else {
      replyToLine(replyToken, `⏳【今日平安確認】\n\n今日 (${todayStr}) 尚未有通過記憶測驗的紀錄，請提醒長輩操作設備完成確認。`);
    }
  } catch (e) {
    replyToLine(replyToken, "⚠️ 平安確認查詢失敗：" + e.toString());
  }
}

function handleGetHistoricalHR(replyToken, args) {
  try {
    let startDate, endDate, rangeLabel;
    if (args && args.length >= 2) {
      startDate = new Date(args[0] + "T00:00:00+08:00");
      endDate = new Date(args[1] + "T23:59:59+08:00");
      rangeLabel = `${args[0]} ~ ${args[1]}`;
    } else {
      endDate = new Date();
      startDate = new Date(endDate.getTime() - 7 * 24 * 60 * 60 * 1000);
      rangeLabel = "近 7 天";
    }

    const records = getSensorDataByTimeRange(null, startDate, endDate).filter(r => r.heartRate > 0);

    if (records.length === 0) {
      replyToLine(replyToken, `📈【健康紀錄分析 (${rangeLabel})】\n\n此期間內尚無有效的心率量測數據。`);
      return;
    }

    const hrs = records.map(r => r.heartRate);
    const avgHr = (hrs.reduce((a, b) => a + b, 0) / hrs.length).toFixed(1);
    const maxHr = Math.max(...hrs);
    const minHr = Math.min(...hrs);

    const spo2s = records.map(r => r.spo2).filter(v => v > 0);
    const avgSpo2 = spo2s.length ? (spo2s.reduce((a, b) => a + b, 0) / spo2s.length).toFixed(1) : "N/A";

    const message =
      `📈【健康紀錄分析 (${rangeLabel})】\n\n` +
      `📊 有效筆數：${records.length} 筆\n` +
      `❤️ 平均心率：${avgHr} BPM\n` +
      `🔺 最高 / 最低心率：${maxHr} / ${minHr} BPM\n` +
      `🩸 平均血氧：${avgSpo2}%`;

    replyToLine(replyToken, message);
  } catch (e) {
    replyToLine(replyToken, "⚠️ 歷史查詢失敗 (格式請用 YYYY-MM-DD)：" + e.toString());
  }
}

function handleAddMedication(replyToken, args) {
  if (!args || args.length === 0) {
    const guide =
      "💊【用藥管理指令說明】\n\n" +
      "• 新增藥品：\n  /add_drug \"藥品名稱\" \"劑量\" 08:00\n" +
      "• 查看清單：\n  /list_drugs\n" +
      "• 修改藥品 (不改請填 -)：\n  /modify_drug M001 \"50mg\" 08:30\n" +
      "• 停用/刪除藥品：\n  /stop_drug M001 或 /delete_drug M001\n" +
      "• 記錄已服藥：\n  /log_medication \"降血壓藥\"";
    replyToLine(replyToken, guide);
    return;
  }

  const [drugName, dosage, scheduleTime] = args;
  if (!drugName || !dosage || !scheduleTime) {
    replyToLine(replyToken, "❌ 參數不足，範例：/add_drug \"降血壓藥\" \"100mg\" 08:00");
    return;
  }

  const created = MedicationConfigManager.addDrug(drugName, dosage, scheduleTime);
  if (created) {
    replyToLine(replyToken, `💊【新增成功】\n編號：${created.id}\n藥品：${created.drugName}\n劑量：${created.dosage}\n時間：${created.scheduleTime}`);
  } else {
    replyToLine(replyToken, "⚠️ 寫入 MedicationSchedule 失敗，請檢查分頁名稱。");
  }
}

function handleListMedications(replyToken) {
  const drugs = MedicationConfigManager.listAllDrugs(false);
  if (drugs.length === 0) {
    replyToLine(replyToken, "💊【用藥排程清單】\n\n目前沒有啟用中的藥品排程。");
    return;
  }

  let msg = "💊【用藥排程清單】\n\n";
  drugs.forEach(d => {
    msg += `• [${d.id}] ${d.drugName} (${d.dosage}) @ ${d.scheduleTime}\n`;
  });
  replyToLine(replyToken, msg);
}

function handleModifyMedication(replyToken, args) {
  const [idOrName, newDosage, newTime] = args;
  if (!idOrName || (!newDosage && !newTime)) {
    replyToLine(replyToken, "❌ 參數錯誤，範例：/modify_drug M001 \"50mg\" 09:00");
    return;
  }

  const ok = MedicationConfigManager.modifyDrug(idOrName, newDosage, newTime);
  replyToLine(replyToken, ok ? `✅ 已更新藥品「${idOrName}」的排程資料。` : `⚠️ 找不到藥品「${idOrName}」。`);
}

function handleDeleteMedication(replyToken, args, isSoftStop) {
  const target = args[0];
  if (!target) {
    replyToLine(replyToken, "❌ 請指定藥品 ID 或名稱，例如：/delete_drug M001");
    return;
  }

  const ok = isSoftStop
    ? MedicationConfigManager.stopDrug(target)
    : MedicationConfigManager.deleteDrug(target);

  const actionText = isSoftStop ? "停用" : "刪除";
  replyToLine(replyToken, ok ? `✅ 已${actionText}藥品「${target}」。` : `⚠️ 找不到藥品「${target}」。`);
}

function handleLogMedication(replyToken, args) {
  const target = args[0];
  if (!target) {
    replyToLine(replyToken, "❌ 請提供藥品 ID 或名稱，範例：/log_medication \"降血壓藥\"");
    return;
  }

  const logged = MedicationConfigManager.logIntake(target);
  if (logged) {
    replyToLine(replyToken, `✅ 已記錄服藥：${logged.drugName} (${logged.dosage})，資料已寫入 MedicationLog。`);
  } else {
    replyToLine(replyToken, `⚠️ 找不到啟用中的藥品「${target}」，請先用 /list_drugs 確認。`);
  }
}

function handleGetDailyIntake(replyToken) {
  try {
    const records = MedicationConfigManager.getDailyIntake();
    if (records.length === 0) {
      replyToLine(replyToken, "📋【今日服藥紀錄】\n\n🔔 今天沒有啟用中的排程藥物。");
      return;
    }

    let listMsg = "📋【今日服藥紀錄】\n\n";
    let countTaken = 0;
    let countPending = 0;

    records.forEach(r => {
      if (r.isTaken) {
        countTaken++;
        listMsg += `✅ [${r.id}] ${r.drugName} (${r.dosage}) @ ${r.scheduledTime} - 已服藥 (${r.intakeTime})\n`;
      } else {
        countPending++;
        listMsg += `⏳ [${r.id}] ${r.drugName} (${r.dosage}) @ ${r.scheduledTime} - 待服藥\n`;
      }
    });

    listMsg += `\n📊 今日摘要：已服用 ${countTaken} 種 / 待服用 ${countPending} 種`;
    replyToLine(replyToken, listMsg);
  } catch (e) {
    replyToLine(replyToken, "⚠️ 今日紀錄查詢失敗：" + e.toString());
  }
}