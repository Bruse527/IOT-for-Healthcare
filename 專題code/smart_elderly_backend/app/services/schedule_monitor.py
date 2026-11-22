from datetime import datetime, timedelta
from app.db.database import SessionLocal
from app.db.models import Device
from app.services.device_manager import DeviceManager
from app.services.line_service import LineService
from app.config.config import settings

class ScheduleMonitor:
    """專門負責排程執行的邏輯，例如定时任務（Health Check, Alerting）。"""
    
    def __init__(self, device_manager: DeviceManager, line_service: LineService):
        self.device_manager = device_manager
        self.line_service = line_service

    def check_timeout_alerts(self):
        """
        定期執行：檢查所有裝置是否超過 alert_timeout_minutes。
        此方法模擬定時任務的執行入口。
        """
        db = SessionLocal()
        try:
            timeout_threshold = datetime.utcnow() - timedelta(minutes=settings.ALERT_TIMEOUT_MINUTES)
            
            # 1. 查詢所有 last_heartbeat 早於閾值的裝置
            # 實際需要使用 ORM 查詢
            devices_to_alert = [
                # Device(device_id=d.device_id, ...) for d in session.query(Device).filter(Device.last_heartbeat < timeout_threshold).all()
            ]
            
            print(f"Scheduler: Found {len(devices_to_alert)} devices requiring timeout alert.")

            for device in devices_to_alert:
                message = f"🚨 緊急警報：裝置 {device.device_id} 已超過 {settings.ALERT_TIMEOUT_MINUTES} 分鐘未報心跳！"
                # 觸發 LineService 發送通知
                self.line_service.send_alert_message(device.line_user_id, message)
                
        finally:
            db.close()

    def run_medication_reminders(self):
        """定期執行：檢查哪些裝置需要接收用藥提醒。"""
        # 邏輯：查詢 MedicationConfig 檢查今天的提醒
        pass