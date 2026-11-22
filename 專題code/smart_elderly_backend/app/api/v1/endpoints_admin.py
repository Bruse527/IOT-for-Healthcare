from fastapi import APIRouter, Request, Depends, HTTPException
from app.config.config import settings
# 待實作：LineSignatureVerifier 函式 (需使用 settings.LINE_CHANNEL_SECRET)

router = APIRouter()

@router.post("/line/verify")
def verify_line_signature(request: Request):
    # 核心邏輯：驗證 LINE Webhook 簽章 (Signature Validation)
    # 如果驗證失敗，返回 400 錯誤。
    print("Received LINE Webhook Signature Verification request.")
    return {"status": "verified"}

@router.post("/line/message")
def handle_line_message(request: Request):
    # 核心邏輯：1. 驗證簽章；2. 解析 message payload；3. 呼叫業務邏輯 (LogProcessor/DeviceManager)
    # 這裡接收到 LINE 的訊息，可能是使用者詢問設定或觸發警報
    print("Received LINE Messaging API message.")
    # 待實作：處理訊息內容
    return {"status": "ok"}

@router.put("/v1/config/{device_id}/medication")
def update_medication_config(device_id: str, data: dict):
    # 核心邏輯：高權限管理員 (需檢查使用者權限) -> 更新 MedicationConfig
    print(f"Attempting to update medication config for {device_id}")
    # 待實作：權限和資料庫更新
    return {"status": "success", "message": "Configuration updated successfully."}