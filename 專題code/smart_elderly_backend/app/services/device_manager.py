from app.db.database import SessionLocal
from app.db.models import Device, SafetyCheckLog, SensorDataLog
from app.config.config import settings

class DeviceManager:
    """負責管理所有裝置的狀態、心跳和身份認證。"""
    
    def __init__(self):
        pass # 依賴注入會處理 DB Session
        
    def update_heartbeat(self, db_session, device_id: str):
        """更新裝置心跳，將狀態設置為 Online。"""
        # 實作 DB 寫入邏輯
        print(f"DB: Device {device_id} heartbeat received.")
        return True

    def get_device_details(self, db_session, device_id: str):
        """獲取裝置的詳細信息。"""
        # 實作 DB 讀取邏輯
        return {"device_id": device_id, "status": "Online"}

    def check_device_status(self, db_session):
        """檢查是否有裝置長時間未報平安或離線。"""
        # 邏輯：查詢 last_heartbeat < (now - ALERT_TIMEOUT_MINUTES)
        pass

class LogProcessor:
    """負責接收和處理原始數據，執行數據驗證和時間校準。"""

    def __init__(self):
        pass

    def validate_and_process_safety_log(self, device_id: str, raw_data: dict, db_session):
        """
        接收安全檢查數據，進行格式和數值驗證。
        @設計考量: 在這裡執行數據去重邏輯 (例如：檢查最近 5 分鐘內是否已記錄此類數據)。
        """
        # 1. 驗證數據 (e.g., accuracy must be 0.0-1.0)
        # 2. 時間校準 (Local Time -> UTC+8 -> UTC Storage)
        # 3. 寫入 SafetyCheckLog
        return True

    def validate_and_process_sensor_data(self, device_id: str, raw_data: dict, db_session):
        """接收感測器數據，進行異常值檢查 (Outlier Detection)。"""
        # 1. 驗證數值範圍 (e.g., HR < 30 or HR > 200)
        # 2. 寫入 SensorDataLog
        return True

class ScheduleMonitor:
    """專門負責排程執行的邏輯，例如定时任務（Health Check, Alerting）。"""
    
    def check_timeout_alerts(self, db_session):
        """定期執行：檢查所有裝置是否超過 alert_timeout_minutes。"""
        print("Scheduler: Running Heartbeat Timeout Check...")
        # 呼叫 DeviceManager 的 check_device_status
        # 如果發現逾時，應觸發 LineService 進行警報通知
        pass
    
    def run_medication_reminders(self, db_session):
        """定期執行：檢查哪些裝置需要接收用藥提醒。"""
        pass

class LineService:
    """封裝與 LINE Messaging API 的所有互動。"""
    
    def __init__(self, access_token: str, channel_secret: str):
        self.access_token = access_token
        self.channel_secret = channel_secret
        # 實際應用中，會初始化 requests session
        
    def send_alert_message(self, target_user_id: str, message_content: str):
        """向 LINE 使用者發送緊急警報。"""
        print(f"LINE: Sending alert to {target_user_id}: {message_content}")
        # 實作 API 呼叫
        pass

    def verify_webhook_signature(self, signature_header: str, payload: bytes) -> bool:
        """驗證 Webhook 簽章的合法性 (核心安全功能)。"""
        print("LINE: Verifying webhook signature...")
        # 實作 HMAC SHA256 驗證
        return True