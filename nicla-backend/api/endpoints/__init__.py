from fastapi import APIRouter
from .device import router as device_router
from .sensor import router as sensor_router
from .files import router as files_router

# 创建主API路由器
router = APIRouter(prefix="/api")

# 注册各个子模块的路由器
router.include_router(device_router, prefix="/devices", tags=["devices"])
router.include_router(sensor_router, prefix="/sensors", tags=["sensors"])
router.include_router(files_router, prefix="/files", tags=["files"])

__all__ = ["router"]