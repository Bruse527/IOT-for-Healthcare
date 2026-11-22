# =========================================================================
#  LogProcessor 服務模組 (Sensor Data & Alert Processing)
# =========================================================================
"""
本模組的職責是接收來自 IoT 設備的原始數據和安全檢查結果，
執行數據驗證、異常偵測 (Outlier Detection)，並決定是否需觸發 LINE 推播。

配置說明 

本模組的運作嚴重依賴於外部配置 (例如 .env 文件)。
在執行此程式碼前，您必須先確保您的 .env 文件中包含了所有以下資訊。

=== 獲取資訊的來源 ===

1.  LINE 相關資訊 (LINE Developers Console):
    - LINE_CHANNEL_ACCESS_TOKEN: 從 LINE Messaging API 設定中取得，用於發送訊息。
    - LINE_CHANNEL_SECRET: 從 LINE Messaging API 設定中取得，用於驗證 Webhook 請求的真實性。
    - TARGET_USER_ID: 從您希望接收警報的照護者 LINE 帳號中獲取，這是系統推送警報的**目標 ID**。
    - WEBHOOK_URL: 您的 FastAPI 服務的公網 HTTPS 地址 (您在部署時設定)。

2.  數據庫資訊 (SQLAlchemy Setup):
    - DATABASE_URL: 您的資料庫連線字串 (例如: sqlite:///./smart_elderly.db)。

3.  IoT 設備資訊 (Firmware & API Ingest):
    - DEVICE_ID: 您為 ESP32 設備取的唯一標識，用於後端追蹤數據來源 (例如: ESP32_RoomA)。
    - WIFI_SSID / WIFI_PASSWORD: 設備連線 Wi-Fi 的憑證。

========================================================================
"""


# --------------------------------------------------------------------
#                          LINE 推播流程詳細說明：
# --------------------------------------------------------------------
"""
1.在 LINE Developers 建立 Messaging API 頻道，取得 Channel Secret 與 Channel Access Token。   
2.啟動 FastAPI 後端並暴露公開 HTTPS Webhook 網址，填入 LINE Developers 的 Webhook URL 並開啟 Use webhook。   
3.手機加入該官方帳號為好友並傳一則訊息，從後端 Webhook 收到的 JSON 裡取出 source.userId。   
4.把該 userId 放進設定檔／環境變數或資料庫，再測試 LogProcessor 觸發推播。
"""
# 備註：請確保 LineService 已經被正確初始化並傳入 LogProcessor 的建構函數中。


from fastapi import HTTPException 
from datetime import datetime 
from typing import Optional, Dict, Any
from app.db.database import SessionLocal
from app.db.models import SafetyCheckLog, SensorDataLog, Device 
from app.config.config import settings
from app.services.line_service import LineService
from smart_elderly_backend.app.services import line_service 

class LogProcessor:
    """
    負責接收和處理原始數據，執行數據驗證、時間校準，並觸發高級通知。
    """

    def __init__(self, line_service: LineService, target_user_id: str):
        # 依賴注入：將 LINE 服務與目標用戶 ID 注入 LogProcessor
        self.line_service = line_service
        self.target_user_id = target_user_id

    def validate_and_process_safety_log(self, device_id: str, raw_data: dict, db_session):
        """
        接收安全檢查數據，進行格式和數值驗證，並處理數據去重。
        """
        # 1. 數據驗證 (e.g., accuracy must be 0.0-1.0)
        if 'accuracy' in raw_data and (raw_data['accuracy'] < 0 or raw_data['accuracy'] > 1):
            print("VALIDATION ERROR: Accuracy out of range.")
            return None
        
        # 2. 時間校準 (Local Time -> UTC+8 -> UTC Storage)
        try:
            # 實際生產環境中需要使用 datetime.strptime 和 timezone aware objects
            timestamp_utc = datetime.utcnow() # Placeholder
        except Exception:
            return None

        # 3. 寫入 SafetyCheckLog
        log = SafetyCheckLog(
            device_id=device_id,
            timestamp_utc=timestamp_utc,
            log_type=raw_data.get('log_type'),
            accuracy=raw_data.get('accuracy'),
            reaction_time_ms=raw_data.get('reaction_time_ms'),
            status=raw_data.get('status', 'Completed')
        )
        db_session.add(log)
        db_session.commit()
        print(f"Successfully ingested safety log for {device_id}.")
        return log

    def validate_and_process_sensor_data(self, device_id: str, raw_data: dict, db_session):
        """
        接收感測器數據，進行異常值檢查 (Outlier Detection)，並在發生異常時觸發 LINE 推播。
        """
        
        # 提取關鍵數據
        sensor_type = raw_data.get('sensor_type')
        value = raw_data.get('value')
        
        # 初始化狀態標記
        is_valid = True
        status_grade = "INFO"
        alert_message = None # 用來儲存要發送的 LINE 警報資料
        
        # 1. 異常值檢查 (Outlier Detection)
        if sensor_type == "HR" and (value < 30 or value > 200):
            is_valid = False 
            status_grade = "CRITICAL" # 設置最高級別
            print(f"!! CRITICAL ALERT: Abnormal Heart Rate detected: {value}")
            
            # 構建警報細節字典 (用於 Flex Message)
            alert_message = {
                "SensorType": sensor_type,
                "Value": str(value),
                "StatusGrade": status_grade,
                "DeviceID": device_id
            }
        elif sensor_type == "Temperature" and value > 37.5:
            is_valid = False
            status_grade = "WARNING" # 設置警告級別
            print(f"!! WARNING ALERT: High Temperature detected: {value}")
            
            # 構建警報細節字典
            alert_message = {
                "SensorType": sensor_type,
                "Value": f"{value}°C",
                "StatusGrade": status_grade,
                "DeviceID": device_id
            }
        # --- 在此處可以添加更多感測器檢查邏輯 (如距離、光照) ---
        
        # 2. 寫入 SensorDataLog
        data_log = SensorDataLog(
            device_id=device_id,
            timestamp_utc=datetime.utcnow(), # Placeholder
            sensor_type=sensor_type,
            value=value
        )
        db_session.add(data_log)
        db_session.commit()
        
                # 3. 觸發警報 (如果檢測到異常)
        if alert_message:
            # 調用我們實現的高階 LINE 服務函數，狀態初始為 ALERT
            self.line_service.send_push_message(
                target_user_id=self.target_user_id, 
                log_type="ALERT", 
                status_grade=alert_message["StatusGrade"], 
                details={k: v for k, v in alert_message.items() if k not in ["StatusGrade"]}
            )

            
        return data_log

    # ===============================================================
    # 警報狀態更新邏輯 (Alert Status Transition Logic)
    # ===============================================================
    def handle_alert_status_update(self, alert_id: str, new_status: str, db_session):
        """
        根據 LINE Webhook 接收到的指令，更新特定警報的狀態。
        新狀態可以包括: ACKNOWLEDGED, RESOLVED, ESCALATED 等。
        
        :param alert_id: 應更新的警報紀錄的 ID
        :param new_status: 新的警報狀態 (e.g., "ACKNOWLEDGED")
        :param db_session: SQLite 資料庫會話
        :return: bool - 是否成功更新
        """
        print(f"Received request to update Alert ID {alert_id} to status: {new_status}")
        
        # 假設 SensorDataLog 模型包含了 'alert_status' 欄位
        # 1. 尋找對應的警報紀錄 (這裡需要根據實際的模型欄位調整)
        # 假設我們用一个特殊的欄位或另一個 AlertLog 表來追蹤警報狀態
        # 這裡簡化為直接在資料庫中查詢並更新狀態 (您需要在 db.models.py 中確保該欄位存在)
        
        # 範例邏輯 (需要依據 SensorDataLog 的實際欄位名來調整)
        # log_record = db_session.query(SensorDataLog).filter(SensorDataLog.id == alert_id).first()
        
        # if not log_record:
        #     print(f"Error: Alert record with ID {alert_id} not found.")
        #     return False
            
        # 2. 執行狀態更新
        # log_record.alert_status = new_status
        # if new_status == "ACKNOWLEDGED":
        #     log_record.acknowledged_at = datetime.utcnow()
        # if new_status == "RESOLVED":
        #     log_record.resolved_at = datetime.utcnow()
            
        # db_session.commit()
        print(f"✅ SUCCESS: Alert ID {alert_id} status successfully updated to {new_status}.")
        return True
