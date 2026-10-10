import logging
from sqlmodel import SQLModel, create_engine, Session, select
from app.config import settings
from app.models import User
from app.auth import hash_password

logger = logging.getLogger("APP_DB")

import time

# Khởi tạo Engine linh hoạt: PostgreSQL (khi chạy Docker) hoặc SQLite (khi test local)
is_sqlite = settings.DATABASE_URL.startswith("sqlite")

if is_sqlite:
    engine = create_engine(
        settings.DATABASE_URL,
        echo=False,
        connect_args={"check_same_thread": False}
    )
else:
    db_url = settings.DATABASE_URL
    if db_url.startswith("postgresql://"):
        db_url = db_url.replace("postgresql://", "postgresql+psycopg2://", 1)
        
    engine = create_engine(
        db_url,
        echo=False,
        pool_pre_ping=True,
        pool_size=10,
        max_overflow=20
    )

def init_db():
    """Khởi tạo cấu trúc các bảng và tạo user Admin mặc định nếu chưa có (với cơ chế retry)"""
    max_retries = 10
    retry_delay = 2

    for attempt in range(1, max_retries + 1):
        try:
            SQLModel.metadata.create_all(engine)
            logger.info("Khởi tạo cấu trúc Database hoàn tất!")

            # Kiểm tra và tạo tài khoản Admin mặc định
            with Session(engine) as session:
                admin_user = session.exec(
                    select(User).where(User.username == settings.DEFAULT_ADMIN_USER)
                ).first()

                if not admin_user:
                    new_admin = User(
                        username=settings.DEFAULT_ADMIN_USER,
                        password_hash=hash_password(settings.DEFAULT_ADMIN_PASS),
                        fullname=settings.DEFAULT_ADMIN_FULLNAME,
                        role="admin",
                        is_active=True
                    )
                    session.add(new_admin)
                    session.commit()
                    logger.info(f"Đã tạo tài khoản Admin mặc định: [{settings.DEFAULT_ADMIN_USER}]")
            return
        except Exception as e:
            logger.warning(f"Lần thử kết nối DB {attempt}/{max_retries} thất bại: {e}")
            if attempt < max_retries:
                time.sleep(retry_delay)
            else:
                logger.error("Không thể kết nối đến Database sau nhiều lần thử!")
                raise e

def get_session():
    """FastAPI Dependency cung cấp Session Database"""
    with Session(engine) as session:
        yield session
