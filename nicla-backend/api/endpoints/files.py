from fastapi import APIRouter, Depends, HTTPException, UploadFile, File, BackgroundTasks
from fastapi.responses import FileResponse, StreamingResponse
from typing import List, Optional
import os

from ..models.file import (
    FileInfo, DownloadProgress, FileUploadRequest, FileOperationResponse, FirmwareUpdateRequest,
    FileType
)
from ...ble_manager import BLEManager
from ...file_service import FileService

router = APIRouter()

# 获取BLE管理器单例
def get_ble_manager():
    return BLEManager.get_instance()

# 获取文件服务
def get_file_service():
    return FileService.get_instance()

@router.get("/list", response_model=List[FileInfo])
async def list_files(
    file_type: Optional[FileType] = None,
    file_service: FileService = Depends(get_file_service)
):
    """列出可用的文件"""
    try:
        files = file_service.list_files(file_type)
        return files
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.get("/info/{filename}", response_model=FileInfo)
async def get_file_info(
    filename: str,
    file_service: FileService = Depends(get_file_service)
):
    """获取文件详细信息"""
    try:
        file_info = file_service.get_file_info(filename)
        if not file_info:
            raise HTTPException(status_code=404, detail="File not found")
        return file_info
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/upload", response_model=FileOperationResponse)
async def upload_file(
    file_type: FileType,
    file: UploadFile = File(...),
    file_service: FileService = Depends(get_file_service)
):
    """上传文件"""
    try:
        # 保存上传的文件
        file_info = await file_service.save_uploaded_file(file, file_type)
        
        return FileOperationResponse(
            success=True,
            message="File uploaded successfully",
            file_info=file_info
        )
    except Exception as e:
        return FileOperationResponse(
            success=False,
            message="Failed to upload file",
            error_message=str(e)
        )

@router.get("/download/{filename}")
async def download_file(
    filename: str,
    file_service: FileService = Depends(get_file_service)
):
    """下载文件"""
    try:
        file_path = file_service.get_file_path(filename)
        if not os.path.exists(file_path):
            raise HTTPException(status_code=404, detail="File not found")
        
        return FileResponse(
            path=file_path,
            filename=filename,
            media_type="application/octet-stream"
        )
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/firmware/update", response_model=FileOperationResponse)
async def update_firmware(
    request: FirmwareUpdateRequest,
    background_tasks: BackgroundTasks,
    ble_manager: BLEManager = Depends(get_ble_manager),
    file_service: FileService = Depends(get_file_service)
):
    """更新设备固件"""
    try:
        # 检查设备连接状态
        if not ble_manager.is_device_connected(request.device_address):
            raise HTTPException(status_code=400, detail="Device not connected")
        
        # 检查固件文件是否存在
        if not os.path.exists(request.firmware_path):
            raise HTTPException(status_code=404, detail="Firmware file not found")
        
        # 验证固件文件
        if not file_service.verify_firmware_file(request.firmware_path):
            raise HTTPException(status_code=400, detail="Invalid firmware file")
        
        # 异步执行固件更新
        background_tasks.add_task(
            ble_manager.update_firmware,
            request.device_address,
            request.firmware_path,
            request.force_update,
            request.backup_settings
        )
        
        return FileOperationResponse(
            success=True,
            message="Firmware update started. Please check progress via WebSocket"
        )
    except HTTPException:
        raise
    except Exception as e:
        return FileOperationResponse(
            success=False,
            message="Failed to start firmware update",
            error_message=str(e)
        )

@router.websocket("/firmware/progress/{device_address}")
async def firmware_update_progress(
    websocket: WebSocket,
    device_address: str,
    ble_manager: BLEManager = Depends(get_ble_manager)
):
    """通过WebSocket获取固件更新进度"""
    await websocket.accept()
    
    try:
        # 注册进度回调
        async def progress_callback(progress: DownloadProgress):
            try:
                await websocket.send_json(progress.dict())
            except Exception:
                # 忽略发送错误
                pass
        
        # 开始接收进度
        ble_manager.register_firmware_progress_callback(device_address, progress_callback)
        
        # 保持连接，直到更新完成或客户端断开
        while True:
            try:
                await websocket.receive_text()
            except WebSocketDisconnect:
                break
    finally:
        # 清理回调
        ble_manager.unregister_firmware_progress_callback(device_address)
        await websocket.close()