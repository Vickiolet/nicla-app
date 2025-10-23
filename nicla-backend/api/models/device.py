from pydantic import BaseModel, Field
from typing import List, Optional
from enum import Enum

class DeviceStatus(str, Enum):
    """设备状态枚举"""
    DISCONNECTED = "disconnected"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    DISCONNECTING = "disconnecting"
    ERROR = "error"

class DeviceInfo(BaseModel):
    """设备基本信息模型"""
    name: str = Field(..., description="设备名称")
    address: str = Field(..., description="设备BLE地址")
    rssi: int = Field(..., description="信号强度")
    manufacturer_data: Optional[dict] = Field(None, description="制造商数据")
    service_uuids: Optional[List[str]] = Field(None, description="设备提供的服务UUID列表")

class DeviceConnection(BaseModel):
    """设备连接状态模型"""
    address: str = Field(..., description="设备BLE地址")
    status: DeviceStatus = Field(..., description="设备连接状态")
    error_message: Optional[str] = Field(None, description="错误信息")
    connected_time: Optional[str] = Field(None, description="连接时间")

class ScanRequest(BaseModel):
    """设备扫描请求模型"""
    timeout: int = Field(default=10, ge=1, le=60, description="扫描超时时间（秒）")
    filter_name: Optional[str] = Field(None, description="设备名称过滤")

class ScanResponse(BaseModel):
    """设备扫描响应模型"""
    devices: List[DeviceInfo] = Field(default_factory=list, description="扫描到的设备列表")
    scan_duration: float = Field(..., description="实际扫描时长")

class ConnectionRequest(BaseModel):
    """设备连接请求模型"""
    address: str = Field(..., description="要连接的设备BLE地址")
    timeout: int = Field(default=30, ge=5, le=60, description="连接超时时间（秒）")