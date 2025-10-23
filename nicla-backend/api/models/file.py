from pydantic import BaseModel, Field, validator
from typing import Optional
from enum import Enum

class FileType(str, Enum):
    """文件类型枚举"""
    FIRMWARE = "firmware"
    CONFIG = "config"
    LOG = "log"
    DATA = "data"

class FileInfo(BaseModel):
    """文件信息模型"""
    filename: str = Field(..., description="文件名")
    filepath: str = Field(..., description="文件路径")
    file_type: FileType = Field(..., description="文件类型")
    size: int = Field(..., ge=0, description="文件大小(字节)")
    modified_time: str = Field(..., description="修改时间")
    checksum: Optional[str] = Field(None, description="文件校验和")

class DownloadProgress(BaseModel):
    """文件下载进度模型"""
    filename: str = Field(..., description="文件名")
    progress: float = Field(..., ge=0, le=100, description="下载进度百分比")
    downloaded_bytes: int = Field(..., ge=0, description="已下载字节数")
    total_bytes: int = Field(..., ge=0, description="总字节数")
    status: str = Field(..., description="下载状态")
    error_message: Optional[str] = Field(None, description="错误信息")

class FileUploadRequest(BaseModel):
    """文件上传请求模型"""
    device_address: str = Field(..., description="目标设备地址")
    file_type: FileType = Field(..., description="文件类型")
    # 文件内容将通过multipart/form-data上传

class FileOperationResponse(BaseModel):
    """文件操作响应模型"""
    success: bool = Field(..., description="操作是否成功")
    message: str = Field(..., description="操作结果消息")
    file_info: Optional[FileInfo] = Field(None, description="文件信息")
    error_message: Optional[str] = Field(None, description="错误信息")

class FirmwareUpdateRequest(BaseModel):
    """固件更新请求模型"""
    device_address: str = Field(..., description="设备地址")
    firmware_path: str = Field(..., description="固件文件路径")
    force_update: bool = Field(default=False, description="是否强制更新")
    backup_settings: bool = Field(default=True, description="是否备份设置")