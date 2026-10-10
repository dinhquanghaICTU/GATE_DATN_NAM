from fastapi import APIRouter, Depends, HTTPException, status
from sqlmodel import Session, select
from typing import List

from app.database import get_session
from app.models import User, UserLoginRequest, UserRegisterRequest, TokenResponse, UserResponse
from app.auth import verify_password, hash_password, create_access_token, get_current_user, require_admin

router = APIRouter(prefix="/auth", tags=["1. Xác thực & Tài khoản (Authentication)"])

@router.post(
    "/login",
    response_model=TokenResponse,
    summary="Đăng nhập hệ thống",
    description="Xác thực username và password, trả về JWT Access Token để gọi các API được bảo vệ."
)
def login(req: UserLoginRequest, session: Session = Depends(get_session)):
    user = session.exec(select(User).where(User.username == req.username)).first()
    if not user or not verify_password(req.password, user.password_hash):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Tên đăng nhập hoặc mật khẩu không chính xác!"
        )
    if not user.is_active:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Tài khoản này hiện đang bị tạm khóa!"
        )

    token_data = {
        "sub": user.username,
        "user_id": user.id,
        "role": user.role,
        "fullname": user.fullname
    }
    access_token = create_access_token(token_data)

    return TokenResponse(
        access_token=access_token,
        token_type="bearer",
        user={
            "id": user.id,
            "username": user.username,
            "fullname": user.fullname,
            "role": user.role
        }
    )

@router.post(
    "/register",
    response_model=UserResponse,
    summary="Đăng ký tài khoản người dùng mới",
    description="Tạo tài khoản mới. Nếu chưa có tài khoản nào, user đầu tiên sẽ là admin."
)
def register(req: UserRegisterRequest, session: Session = Depends(get_session)):
    existing = session.exec(select(User).where(User.username == req.username)).first()
    if existing:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=f"Tên tài khoản '{req.username}' đã tồn tại trong hệ thống!"
        )

    new_user = User(
        username=req.username,
        password_hash=hash_password(req.password),
        fullname=req.fullname or req.username,
        role=req.role or "operator",
        is_active=True
    )
    session.add(new_user)
    session.commit()
    session.refresh(new_user)
    return new_user

@router.get(
    "/me",
    summary="Lấy thông tin tài khoản hiện tại",
    description="Truy xuất thông tin người dùng đang đăng nhập dựa trên JWT Token trong Header."
)
def get_me(current_user: dict = Depends(get_current_user), session: Session = Depends(get_session)):
    user = session.exec(select(User).where(User.username == current_user["username"])).first()
    if not user:
        raise HTTPException(status_code=404, detail="Không tìm thấy người dùng")
    return {
        "id": user.id,
        "username": user.username,
        "fullname": user.fullname,
        "role": user.role,
        "is_active": user.is_active,
        "created_at": user.created_at
    }

@router.get(
    "/users",
    response_model=List[UserResponse],
    summary="Danh sách người dùng (Chỉ Admin)",
    description="Xem danh sách tất cả tài khoản trong hệ thống. Yêu cầu quyền Admin."
)
def list_users(
    session: Session = Depends(get_session),
    admin: dict = Depends(require_admin)
):
    users = session.exec(select(User)).all()
    return users
