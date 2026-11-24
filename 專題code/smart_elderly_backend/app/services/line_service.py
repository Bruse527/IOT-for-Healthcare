import hmac
import hashlib
import json
import requests # 引入 HTTP 請求庫
from typing import Dict, Optional
from app.config.config import settings # 假設您的設定檔存在此路徑

class LineService:
    """封裝與 LINE Messaging API 的所有互動，處理訊息送發與安全驗證。"""

    def __init__(self):
        self.access_token = settings.LINE_CHANNEL_ACCESS_TOKEN 
        self.channel_secret = settings.LINE_CHANNEL_SECRET
        self.api_url = "https://api.line.me/v2/bot/message/push" 
        
        if not self.access_token or not self.channel_secret:
            print("WARNING: LINE tokens are not set in environment variables. Notification services are disabled.")
            
    def verify_webhook_signature(self, signature_header: str, payload: bytes) -> bool:
        """
        驗證 Webhook 簽章的合法性。
        """
        try:
            # 使用指定的密鑰計算期望的簽章
            mac = hmac.new(self.channel_secret.encode('utf-8'), payload, hashlib.sha256)
            expected_signature = mac.hexdigest()
            
            # 比較簽章，確保安全
            return hmac.compare_digest(signature_header, expected_signature)
        except Exception as e:
            print(f"LINE Signature Verification Failed: {e}")
            return False
            
    def _create_flex_message(self, log_type: str, status_grade: str, details: Dict) -> Dict:
        """
        將 IoT 數據結構轉換為 LINE Flex Message JSON 格式。
        這是專案專業度的關鍵展示點！
        """
        
        # 基礎結構定義
        card_content = {
            "type": "bubble",
            "body": {
                "type": "box",
                "layout": "vertical",
                "contents": [
                    {
                        "type": "text",
                        "text": f"【{status_grade}】{log_type} 事件通知", 
                        "weight": "bold",
                        "size": "lg",
                        "color": self._get_color_by_grade(status_grade) # 根據級別自動設定顏色
                    },
                    {
                        "type": "text",
                        "text": "最新的健康數據和狀態資訊如下:",
                        "wrap": True,
                        "size": "md"
                    },
                    {
                        "type": "box",
                        "layout": "vertical",
                        "contents": [
                            # 將 IoT 傳來的 details 資訊以列表形式顯示
                            *[{"type": "text", "text": f"{k}: {v}"} for k, v in details.items()]
                        ],
                        "margin": "md"
                    }
                ],
                "paddingAll": "15px"
            },
            # 增加操作按鈕 (CTA)
            "actions": [
                # 針對不同事件設置不同的按鈕動作
                {"type": "button", "action": {"type": "message", "text": "✅ 確認收到警報"}, "label": "確認收到"},
                {"type": "button", "action": {"type": "uri", "label": "查看歷史記錄", "uri": "https://your-dashboard-url.com"}}
            ]
        }
        return card_content

    def _get_color_by_grade(self, grade: str) -> str:
        """根據狀態級別返回對應的顏色代碼"""
        if grade == "CRITICAL":
            return "#FF4500" # 紅色 (危險)
        elif grade == "WARNING":
            return "#FFD700" # 黃色 (警告)
        elif grade == "INFO":
            return "#00A86B" # 綠色 (正常/信息)
        else:
            return "#FFFFFF" # 白色 (預設)


    def send_push_message(self, target_user_id: str, log_type: str, status_grade: str, details: Dict):
        """
        根據結構化資料，發送專業的 Flex Message 推播到 LINE。
        這就是 LogProcessor 最終調用的高階函數。
        """
        if not self.access_token:
            print("SYSTEM ERROR: Access Token not available. Cannot send LINE notification.")
            return

        # 1. 創建富媒體卡片內容
        message_payload = self._create_flex_message(log_type, status_grade, details)

        # 2. 構造 API 請求的最終 JSON 體
        payload = {
            "to": target_user_id,
            "messages": [
                {"type": "flex", "altText": f"【{status_grade}】{log_type} 事件通知", "contents": message_payload}
            ]
        }

        # 3. 發送請求
        headers = {
            "Content-Type": "application/json",
            "Authorization": f"Bearer {self.access_token}"
        }
        
        try:
            # 執行 POST 請求
            response = requests.post(self.api_url, headers=headers, data=json.dumps(payload))
            response.raise_for_status() # 拋出 HTTP 錯誤
            print(f"LINE SUCCESS: Message sent successfully to {target_user_id}. Status: {response.status_code}")
        except requests.exceptions.RequestException as e:
            print(f"LINE FAILED: Error sending message to LINE API: {e}")

    def process_incoming_message(self, payload: Dict):
        """
        解析並處理來自 LINE 的訊息內容 (例如：用戶點擊了確認按鈕)。
        """
        print("LINE: Processing incoming message payload...")
        # TODO: 實作邏輯，當收到 "確認收到警報" 的訊息時，通知後端 LogProcessor 將警報狀態從 ALERT 轉為 RECOVERY
        pass
