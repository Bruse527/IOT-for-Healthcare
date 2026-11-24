from sqlalchemy import Column, Integer, String, Float, DateTime, Boolean, ForeignKey
from sqlalchemy.dialects.sqlite import JSON 
from datetime import datetime
from typing import Optional
from .database import Base 

# ----------------------------------------------------------
# 📌 資料表定義
# ----------------------------------------------------------

class Device(Base):
    __tablename__ = "devices"
    device_id = Column(String, primary_key=True, index=True)
    device_name = Column(String, nullable=False)
    status = Column(String, default="Offline") # Online, Offline, Pending
    last_heartbeat = Column(DateTime, nullable=True) # 記錄最後心跳時間
    line_user_id = Column(String, nullable=True) # 用於 LINE 綁定的使用者ID

class DeviceSetting(Base):
    __tablename__ = "device_settings"
    device_id = Column(String, ForeignKey("devices.device_id"), primary_key=True)
    setting_key = Column(String, primary_key=True) # 例如: "alert_timeout", "notification_toggle"
    setting_value = Column(String) # 儲存設定值 (如: "12", "True")
    updated_at = Column(DateTime, default=datetime.utcnow)

class MedicationConfig(Base):
    __tablename__ = "medication_config"
    med_id = Column(Integer, primary_key=True, autoincrement=True)
    device_id = Column(String, ForeignKey("devices.device_id"), nullable=False)
    name = Column(String, nullable=False)
    instruction = Column(String)
    start_date = Column(DateTime)
    end_date = Column(DateTime)
    is_repeating = Column(Boolean, default=False)
    is_enabled = Column(Boolean, default=True)

class SafetyCheckLog(Base):
    __tablename__ = "safety_check_logs"
    log_id = Column(Integer, primary_key=True, autoincrement=True)
    device_id = Column(String, ForeignKey("devices.device_id"), nullable=False)
    timestamp_utc = Column(DateTime, nullable=False) # UTC+8 的基準時間
    log_type = Column(String, nullable=False) # "Cognitive", "SafetyCheck"
    accuracy = Column(Float) # 認知測驗的正確性 (0.0 - 1.0)
    reaction_time_ms = Column(Integer)
    status = Column(String, nullable=False) # "Completed", "NoResponse", "MeasurementFailed"

class SensorDataLog(Base):
    __tablename__ = "sensor_data_logs"
    sensor_id = Column(Integer, primary_key=True, autoincrement=True)
    device_id = Column(String, ForeignKey("devices.device_id"), nullable=False)
    timestamp_utc = Column(DateTime, nullable=False)
    sensor_type = Column(String, nullable=False) # "HR", "SpO2", "Distance", "Temp"
    value = Column(Float, nullable=False)

class PendingEvent(Base):
    __tablename__ = "pending_events"
    event_id = Column(Integer, primary_key=True, autoincrement=True)
    device_id = Column(String, ForeignKey("devices.device_id"), nullable=False)
    timestamp_event = Column(DateTime, nullable=False) # 事件發生時間
    event_payload = Column(String, nullable=False) # 儲存序列化的 JSON payload
    attempt_count = Column(Integer, default=0)