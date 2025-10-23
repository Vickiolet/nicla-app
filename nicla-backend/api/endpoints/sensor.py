from fastapi import APIRouter, Depends, HTTPException, WebSocket, WebSocketDisconnect
from typing import List

from ..models.sensor import (
    SensorConfig, SensorData, SensorConfigRequest, SensorConfigResponse, SensorDataStreamRequest
)
from ...ble_manager import BLEManager
from ...sensor_config import SensorConfigManager

router = APIRouter()

# 获取BLE管理器单例
def get_ble_manager():
    return BLEManager.get_instance()

# 获取传感器配置管理器
def get_sensor_config_manager():
    return SensorConfigManager.get_instance()

@router.get("/{device_address}/available", response_model=List[dict])
async def get_available_sensors(
    device_address: str,
    ble_manager: BLEManager = Depends(get_ble_manager)
):
    """获取设备支持的传感器列表"""
    try:
        if not ble_manager.is_device_connected(device_address):
            raise HTTPException(status_code=400, detail="Device not connected")
        
        sensors = await ble_manager.get_available_sensors(device_address)
        return sensors
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.get("/{device_address}/config", response_model=List[SensorConfig])
async def get_sensor_configs(
    device_address: str,
    sensor_config_manager: SensorConfigManager = Depends(get_sensor_config_manager)
):
    """获取设备当前的传感器配置"""
    try:
        configs = sensor_config_manager.get_device_configs(device_address)
        return configs
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/config", response_model=SensorConfigResponse)
async def set_sensor_configs(
    request: SensorConfigRequest,
    ble_manager: BLEManager = Depends(get_ble_manager),
    sensor_config_manager: SensorConfigManager = Depends(get_sensor_config_manager)
):
    """设置设备的传感器配置"""
    try:
        if not ble_manager.is_device_connected(request.device_address):
            raise HTTPException(status_code=400, detail="Device not connected")
        
        # 验证配置
        for config in request.configs:
            if not sensor_config_manager.validate_sensor_config(config):
                raise HTTPException(
                    status_code=400,
                    detail=f"Invalid configuration for sensor {config.sensor_type}"
                )
        
        # 应用配置
        applied_configs = await sensor_config_manager.apply_configs(
            request.device_address,
            request.configs,
            request.apply_immediately
        )
        
        return SensorConfigResponse(
            device_address=request.device_address,
            success=True,
            applied_configs=applied_configs
        )
    except HTTPException:
        raise
    except Exception as e:
        return SensorConfigResponse(
            device_address=request.device_address if hasattr(request, 'device_address') else '',
            success=False,
            error_message=str(e)
        )

@router.websocket("/{device_address}/data-stream")
async def sensor_data_stream(
    websocket: WebSocket,
    device_address: str,
    ble_manager: BLEManager = Depends(get_ble_manager)
):
    """通过WebSocket获取传感器数据流"""
    await websocket.accept()
    
    try:
        # 检查设备连接状态
        if not ble_manager.is_device_connected(device_address):
            await websocket.send_json({
                "error": "Device not connected"
            })
            await websocket.close()
            return
        
        # 注册数据回调
        async def data_callback(sensor_data: SensorData):
            try:
                await websocket.send_json(sensor_data.dict())
            except Exception:
                # 忽略发送错误，通常是因为客户端已断开连接
                pass
        
        # 开始接收数据
        ble_manager.register_data_callback(device_address, data_callback)
        
        # 保持连接，直到客户端断开
        while True:
            # 定期检查连接状态
            if not ble_manager.is_device_connected(device_address):
                await websocket.send_json({"error": "Device disconnected"})
                break
            
            # 等待客户端消息（心跳或配置更新）
            try:
                await websocket.receive_text()
            except WebSocketDisconnect:
                break
            except Exception:
                # 忽略接收错误
                pass
    finally:
        # 清理回调
        ble_manager.unregister_data_callback(device_address)
        await websocket.close()