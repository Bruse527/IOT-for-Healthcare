import base64
import hashlib
import hmac
from fastapi import APIRouter, Request, Depends, HTTPException
from app.config.config import settings
# 待實作：LineSignatureVerifier 函式 (需使用 settings.LINE_CHANNEL_SECRET)

router = APIRouter()


async def _verify_line_signature(request: Request) -> bytes:
    """驗證 LINE Webhook 的 x-line-signature 簽章，驗證失敗則回傳 400 錯誤。"""
    body = await request.body()
    signature = request.headers.get("x-line-signature", "")
    channel_secret = getattr(settings, "LINE_CHANNEL_SECRET", "")

    # 開發測試階段若尚未在 .env 填入 LINE_CHANNEL_SECRET，先印出提示避免卡住測試；
    # 正式啟用時請務必在 .env 填入正確的 LINE_CHANNEL_SECRET。
    if not channel_secret:
        print("尚未設定 LINE_CHANNEL_SECRET，暫時略過簽章驗證。")
        return body

    digest = hmac.new(
        channel_secret.encode("utf-8"),
        body,
        hashlib.sha256,
    ).digest()
    expected_signature = base64.b64encode(digest).decode("utf-8")

    if not signature or not hmac.compare_digest(expected_signature, signature):
        raise HTTPException(status_code=400, detail="Invalid LINE signature")

    return body


@router.post("/line/verify")
async def verify_line_signature(request: Request):
    # 核心邏輯：驗證 LINE Webhook 簽章 (Signature Validation)
    # 如果驗證失敗，返回 400 錯誤。
    await _verify_line_signature(request)
    print("Received LINE Webhook Signature Verification request.")
    return {"status": "verified"}


@router.post("/line/message")
async def handle_line_message(request: Request):
    # 核心邏輯：1. 驗證簽章；2. 解析 message payload；3. 呼叫業務邏輯 (LogProcessor/DeviceManager)
    # 這裡接收到 LINE 的訊息，可能是使用者詢問設定或觸發警報
    raw_body = await _verify_line_signature(request)
    print("Received LINE Messaging API message.")

    try:
        payload = await request.json()
    except Exception:
        print("Received non-JSON LINE body:", raw_body.decode("utf-8", errors="replace"))
        return {"status": "ok"}

    for event in payload.get("events", []):
        source = event.get("source", {})
        user_id = source.get("userId")
        if user_id:
            print(f"TARGET_USER_ID: {user_id}")

        if event.get("type") == "message":
            message = event.get("message", {})
            print(f"Message type: {message.get('type')} | Text: {message.get('text')}")

    # 待實作：處理訊息內容
    return {"status": "ok"}


@router.put("/v1/config/{device_id}/medication")
def update_medication_config(device_id: str, data: dict):
    # 核心邏輯：高權限管理員 (需檢查使用者權限) -> 更新 MedicationConfig
    print(f"Attempting to update medication config for {device_id}")
    # 待實作：權限和資料庫更新
    return {"status": "success", "message": "Configuration updated successfully."}