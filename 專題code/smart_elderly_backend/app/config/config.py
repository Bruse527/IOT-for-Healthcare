"""
1. 建立或確認 LINE 官方帳號
前往 LINE Biz-Solutions，使用你的 LINE 帳號或 LINE Business ID 登入。

如果還沒有官方帳號，點選建立並填寫帳號名稱、業態與業務內容完成建立（免費的「未認證」帳號就可以用來測試 Messaging API）。

確認左上角切換到你要用來做智慧照護系統的正確官方帳號。

2. 在官方帳號後台啟用 Messaging API
在官方帳號後台點選右上角的 「設定」（Settings）。

切到左側選單的 「Messaging API」，按下 「啟用 Messaging API」（Enable Messaging API）。

選擇或建立 Provider：

若是第一次使用，選擇建立新的 Provider（例如可填專案或團隊名稱）。

Provider 選定後無法直接更換，之後若要串接同系列服務建議放在同一個 Provider 下。

閱讀並同意條款後完成啟用，系統會自動產生對應的 Messaging API Channel。

3. 到 LINE Developers Console 取得金鑰與設定 Webhook
前往 LINE Developers Console，用同一個帳號登入（若是首次登入，先填寫開發者名稱與 Email）。

進入剛剛選定的 Provider，點開自動建立的 Messaging API Channel。

取得憑證（放進後端環境變數 / 設定檔，切勿推上公開 Git）：

Channel secret：在 Basic settings 分頁找到，用來驗證 Webhook 請求的 x-line-signature 簽章。

Channel access token：在 Messaging API 分頁最下方按 Issue 產生，用來讓 FastAPI 後端呼叫 LINE API 發送推播或回覆訊息。

設定 Webhook（做 B 階段雙向互動與抓 User ID 時需要）：

在 Messaging API 分頁的 Webhook settings 填入你的公開 HTTPS Webhook URL，按 Update。

按 Verify 測試（你的 FastAPI 端點需能接收 POST 並回傳 HTTP 200）。

確認 Use webhook 開關已開啟。
"""
import os
from pydantic import BaseModel, Field
from typing import Optional

class Settings(BaseModel):
    # --- 系統設定 ---
    ENVIRONMENT: str = Field("development", description="運行環境: development/production")
    SECRET_KEY: str = Field(..., description="FastAPI 安全密鑰")
    
    # --- 資料庫設定 ---
    DATABASE_URL: str = Field("sqlite:///./smart_elderly.db", description="SQLite 資料庫連線字串")
    
    # --- LINE Messaging API 設定 ---
    LINE_CHANNEL_ACCESS_TOKEN: Optional[str] = Field(None, description="LINE 訊息傳送的 Access Token")
    LINE_CHANNEL_SECRET: Optional[str] = Field(None, description="LINE Webhook 簽章密鑰")

    # --- 監測設定 ---
    ALERT_TIMEOUT_MINUTES: int = Field(12, description="平安確認逾時警報的預設分鐘數")
    
    # 靜態方法用於從環境變數載入配置
    @classmethod
    def get_settings(cls):
        try:
            return cls(
                SECRET_KEY=os.environ.get("SECRET_KEY", "default_secret"),
                DATABASE_URL=os.environ.get("DATABASE_URL", "sqlite:///./smart_elderly.db"),
                LINE_CHANNEL_ACCESS_TOKEN=os.environ.get("LINE_CHANNEL_ACCESS_TOKEN"),
                LINE_CHANNEL_SECRET=os.environ.get("LINE_CHANNEL_SECRET"),
                ALERT_TIMEOUT_MINUTES=int(os.environ.get("ALERT_TIMEOUT_MINUTES", 12))
            )
        except Exception as e:
            print(f"FATAL: 環境變數載入失敗: {e}")
            # 在實際環境中應 raise 錯誤或使用更穩健的錯誤處理
            exit(1)

# 創建單例實例，確保整個應用程式使用一致的配置
settings = Settings.get_settings()