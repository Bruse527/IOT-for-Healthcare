from sqlalchemy.orm import Session
from abc import ABC, abstractmethod
from typing import List, Optional, TypeVar
from app.db.models import Device, SensorDataLog, SafetyCheckLog, MedicationConfig
from sqlalchemy import update, select
from datetime import datetime

# Type variable for generic repository
T = TypeVar('T')

class BaseRepository(ABC):
    """
    抽象基底資料庫 Repository。
    所有具體的 Repository 都必須繼承此類。
    """
    def __init__(self, db: Session):
        self.db = db

    @abstractmethod
    def get(self, pk: str) -> Optional[T]:
        """根據主鍵獲取單個實體。"""
        pass

    @abstractmethod
    def create(self, entity: T) -> T:
        """創建並保存一個新的實體。"""
        pass

    @abstractmethod
    def update(self, entity: T) -> T:
        """更新一個現有的實體。"""
        pass

    @abstractmethod
    def get_all(self) -> List[T]:
        """獲取所有實體列表。"""
        pass
        
class DeviceRepository(BaseRepository):
    """
    專門處理 Device 模型的所有資料庫操作。
    """
    def __init__(self, db: Session):
        super().__init__(db)
        
    def get(self, device_id: str) -> Optional[Device]:
        """根據 Device ID 獲取裝置信息。"""
        stmt = select(Device).where(Device.device_id == device_id)
        return self.db.execute(stmt).scalar_one_or_none()

    def create(self, device: Device) -> Device:
        """創建新的裝置記錄。"""
        self.db.add(device)
        self.db.commit()
        self.db.refresh(device)
        return device

    def update(self, device: Device) -> Device:
        """更新裝置狀態或心跳時間。"""
        # 為了簡化，我們假設傳入的 entity 已經是從 DB 讀取的
        self.db.add(device)
        self.db.commit()
        self.db.refresh(device)
        return device

    def update_status_by_id(self, device_id: str, status: str, last_heartbeat: datetime) -> bool:
        """
        批量更新裝置的狀態和心跳時間。
        使用 SQLAlchemy 的 update() 語法來避免載入完整的 Session。
        """
        stmt = (
            update(Device)
            .where(Device.device_id == device_id)
            .values(status=status, last_heartbeat=last_heartbeat)
        )
        result = self.db.execute(stmt)
        self.db.commit()
        return result.rowcount > 0

    def get_all_active(self) -> List[Device]:
        """獲取所有狀態為 Online 的裝置。"""
        stmt = select(Device).where(Device.status == "Online")
        return self.db.execute(stmt).scalars().all()

class SensorDataRepository(BaseRepository):
    """
    專門處理 SensorDataLog 模型的所有資料庫操作。
    """
    def __init__(self, db: Session):
        super().__init__(db)

    def create(self, log_entry: SensorDataLog) -> SensorDataLog:
        """創建新的感測器數據記錄。"""
        self.db.add(log_entry)
        self.db.commit()
        self.db.refresh(log_entry)
        return log_entry

    def get_by_device_and_type(self, device_id: str, sensor_type: str) -> List[SensorDataLog]:
        """根據裝置 ID 和感測器類型獲取所有數據。"""
        stmt = (
            select(SensorDataLog)
            .where(SensorDataLog.device_id == device_id)
            .where(SensorDataLog.sensor_type == sensor_type)
            .order_by(SensorDataLog.timestamp_utc.desc())
        )
        return self.db.execute(stmt).scalars().all()

    def get_latest_vitals(self, device_id: str) -> List[dict]:
        """獲取指定裝置最新的幾組生命體徵數據 (e.g., HR, SpO2)。"""
        # 實作更複雜的查詢，例如在不同 sensor_type 上取最近的記錄
        print("Repository: Querying latest vitals for optimization.")
        return []

class SafetyCheckRepository(BaseRepository):
    """
    專門處理 SafetyCheckLog 模型的所有資料庫操作。
    """
    def __init__(self, db: Session):
        super().__init__(db)
        
    def create(self, log_entry: SafetyCheckLog) -> SafetyCheckLog:
        """創建新的安全檢查記錄。"""
        self.db.add(log_entry)
        self.db.commit()
        self.db.refresh(log_entry)
        return log_entry

    def get_recent_logs(self, device_id: str, limit: int = 20) -> List[SafetyCheckLog]:
        """根據裝置 ID 獲取最近的安全檢查記錄。"""
        stmt = (
            select(SafetyCheckLog)
            .where(SafetyCheckLog.device_id == device_id)
            .order_by(SafetyCheckLog.timestamp_utc.desc())
            .limit(limit)
        )
        return self.db.execute(stmt).scalars().all()
        
# 註：其他 Repository (MedicationConfigRepository, PendingEventRepository) 將在後續擴展中加入。