# --------------------------------------------------------------------
#                          LINE 推播流程說明：
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
# 假設您的數據庫和配置結構保持不變
from app.db.database import SessionLocal
from app.db.models import SafetyCheckLog, SensorDataLog, Device 
from app.config.config import settings
from app.services.line_service import LineService 

class LogProcessor:
    """
    負責接收和處理原始數據，執行數據驗證、時間校準，並觸發高級通知。
    """

    def __init__(self, line_service: LineService):
        # 依賴注入：將 LINE 服務注入 LogProcessor
        self.line_service = line_service
        
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
            # 調用我們實現的高階 LINE 服務函數
            self.line_service.send_push_message(
                target_user_id="UserID XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXxx ", # TODO: 替換為目標用戶 ID
                log_type="ALERT", 
                status_grade=alert_message["StatusGrade"], 
                details={k: v for k, v in alert_message.items() if k not in ["StatusGrade"]}
            )
            
        return data_log


