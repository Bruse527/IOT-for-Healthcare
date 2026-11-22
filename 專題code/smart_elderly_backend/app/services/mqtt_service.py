import json
import paho.mqtt.client as mqtt
from typing import Dict, Any

# 引入配置和Repository (假設這些路徑是正確的)
from app.config.config import settings
from app.db.repository import DeviceRepository, SafetyCheckRepository, SensorDataRepository, PendingEventRepository
from app.db.models import Device, SafetyCheckLog, SensorDataLog, PendingEvent
from datetime import datetime

class MqttService:
    """
    MQTT 訊息訂閱服務，負責接收來自 ESP32 裝置的即時數據。
    這就是後端系統與 IoT 設備溝通的橋樑。
    """
    def __init__(self, device_repo: DeviceRepository, safety_repo: SafetyCheckRepository, sensor_repo: SensorDataRepository, pending_event_repo: PendingEventRepository):
        """
        初始化 MQTT 客戶端並注入所有必要的資料庫 Repository。
        """
        self.device_repo = device_repo
        self.safety_repo = safety_repo
        self.sensor_repo = sensor_repo
        self.pending_event_repo = pending_event_repo
        
        # MQTT Broker 配置
        self.broker_host = settings.mqtt_broker_host
        self.broker_port = settings.mqtt_broker_port
        self.client = mqtt.Client(f"Backend_Receiver_{datetime.now().strftime('%Y%m%d%H%M%S')}")
        
        # 設定回呼函數
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect

    def _on_connect(self, client, userdata, flags, rc):
        """當成功連線到 MQTT Broker 時觸發。"""
        if rc == 0:
            print(f"✅ MQTT: Successfully connected to broker {self.broker_host}:{self.broker_port}")
            # 訂閱所有可能的裝置和主題
            self.client.subscribe("smart_elderly/+/+")
            print("✅ MQTT: Subscribed to smart_elderly/+/+ topic.")
        else:
            print(f"❌ MQTT: Connection failed with code {rc}")

    def _on_disconnect(self, client, userdata, rc):
        """當斷線時觸發。"""
        print(f"⚠️ MQTT: Disconnected from broker. Return code: {rc}")

    def _on_message(self, client, userdata, msg):
        """
        核心訊息處理函數。當收到任何訂閱主題的消息時會呼叫。
        msg.payload 是來自 ESP32 的原始數據 (bytes)。
        """
        try:
            payload_str = msg.payload.decode("utf-8")
            data: Dict[str, Any] = json.loads(payload_str)
            topic = msg.topic
            
            print(f"---------------------------------------------------------")
            print(f"📥 RECEIVED MESSAGE on Topic: {topic}")
            print(f"Payload Data: {data}")

            # 根據主題結構解析 Device ID (假設主題結構為: smart_elderly/device/{device_id}/{sub_topic})
            topic_parts = topic.split('/')
            if len(topic_parts) < 4 or topic_parts[1] != "device":
                print("❌ Error: Topic format incorrect. Skipping message.")
                return
                
            device_id = topic_parts[2]
            sub_topic = topic_parts[3]

            print(f"-> Processing message for Device ID: {device_id} from Sub-topic: {sub_topic}")

            # 路由到不同的處理器
            if sub_topic == "heartbeat":
                self._handle_heartbeat(device_id, data)
            elif sub_topic == "safety_event":
                self._handle_safety_event(device_id, data)
            elif sub_topic == "data_log":
                self._handle_sensor_data(device_id, data)
            elif sub_topic == "sync_report":
                self._handle_sync_report(device_id, data)
            else:
                print(f"❌ Unknown sub-topic: {sub_topic}")

        except json.JSONDecodeError:
            print("❌ Error: Received payload is not valid JSON.")
        except Exception as e:
            print(f"❌ CRITICAL ERROR during message handling: {e}")
