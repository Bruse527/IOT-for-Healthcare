from sqlalchemy import create_engine
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker
from app.config.config import settings

# 創建 SQLAlchemy 引擎
engine = create_engine(
    settings.DATABASE_URL, 
    connect_args={"check_same_thread": False} # 允許跨執行緒訪問
)

# 定義所有模型的基礎類別
Base = declarative_base()

# 創建 Session Maker
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)

def get_db():
    """
    一個依賴注入的生成器，用於提供資料庫會話。
    """
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()

def init_db():
    """
    初始化資料庫：確保所有模型（表格）都被建立。
    此函式應在應用程式啟動時執行一次。
    """
    print("🚀 正在檢查和初始化資料庫結構...")
    try:
        # 導入 models 以確保 Base.metadata 知道要創建哪些表格
        from app.db import models 
        Base.metadata.create_all(bind=engine)
        print("✅ 資料庫結構初始化完成：所有表格已就緒。")
    except Exception as e:
        print(f"❌ 資料庫初始化失敗：{e}")