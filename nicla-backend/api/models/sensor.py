from pydantic import BaseModel, Field, validator
from typing import List, Dict, Optional, Union
from enum import Enum

class SensorType(str, Enum):
    """传感器类型枚举"""
    ACCELEROMETER = "accelerometer"
    GYROSCOPE = "gyroscope"
    MAGNETOMETER = "magnetometer"
    TEMPERATURE = "temperature"
    HUMIDITY = "humidity"
    PRESSURE = "pressure"
    GAS = "gas"
    PROXIMITY = "proximity"
    LIGHT = "light"

class SensorRange(str, Enum):
    """传感器测量范围枚举"""
    RANGE_2G = "2g"
    RANGE_4G = "4g"
    RANGE_8G = "8g"
    RANGE_16G = "16g"
    RANGE_250DPS = "250dps"
    RANGE_500DPS = "500dps"
    RANGE_1000DPS = "1000dps"
    RANGE_2000DPS = "2000dps"

class SensorConfig(BaseModel):
    """单个传感器配置模型"""
    sensor_type: SensorType = Field(..., description="传感器类型")
    enabled: bool = Field(default=False, description="是否启用")
    sample_rate: int = Field(default=10, ge=1, le=1000, description="采样率(Hz)")
    sensor_range: Optional[SensorRange] = Field(None, description="测量范围")
    resolution: Optional[int] = Field(None, ge=8, le=24, description="分辨率(位)")
    threshold: Optional[float] = Field(None, description="阈值")

class SensorConfigRequest(BaseModel):
    """传感器配置请求模型"""
    device_address: str = Field(..., description="设备地址")
    configs: List[SensorConfig] = Field(default_factory=list, description="传感器配置列表")
    apply_immediately: bool = Field(default=True, description="是否立即应用配置")

class SensorData(BaseModel):
    """传感器数据模型"""
    sensor_type: SensorType = Field(..., description="传感器类型")
    timestamp: float = Field(..., description="时间戳")
    values: List[float] = Field(..., description="传感器读数")
    unit: str = Field(..., description="单位")

class SensorConfigResponse(BaseModel):
    """传感器配置响应模型"""
    device_address: str = Field(..., description="设备地址")
    success: bool = Field(..., description="配置是否成功")
    applied_configs: List[SensorConfig] = Field(default_factory=list, description="实际应用的配置")
    error_message: Optional[str] = Field(None, description="错误信息")

class SensorDataStreamRequest(BaseModel):
    """传感器数据流请求模型"""
    device_address: str = Field(..., description="设备地址")
    sensor_types: List[SensorType] = Field(default_factory=list, description="要订阅的传感器类型")
    duration: Optional[int] = Field(None, description="数据采集时长(秒)")