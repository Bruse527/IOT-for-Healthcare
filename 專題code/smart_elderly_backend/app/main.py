from fastapi import FastAPI
from app.config.config import settings
from app.db.database import init_db, get_db

app = FastAPI(title="Elderly Care Backend", version="1.0.0")

@app.on_event("startup")
def on_startup():
    # 應用程式啟動時初始化資料庫
    init_db()

# 這裡會註冊所有 v1 的路由
from app.api.v1 import endpoints_data, endpoints_query, endpoints_admin
app.include_router(endpoints_data.router, prefix="/v1/data", tags=["Data Ingestion"])
app.include_router(endpoints_query.router, prefix="/v1/status", tags=["Query"])
app.include_router(endpoints_admin.router, prefix="/webhook", tags=["Admin/Webhook"])

@app.get("/")
def read_root():
    return {"service": "Elderly Care Backend API", "status": "Running"}