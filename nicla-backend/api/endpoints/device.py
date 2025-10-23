from fastapi import APIRouter, Depends, HTTPException, BackgroundTasks
from typing import List
import time

from ..models.device import (
    DeviceInfo, DeviceConnection, ScanRequest, ScanResponse, ConnectionRequest
)
from ...ble_manager import BLEManager

router = APIRouter()

# 获取BLE管理器单例
def get_ble_manager():
    return BLEManager.get_instance()

@router.get("/connected", response_model=List[DeviceConnection])
async def get_connected_devices(
    ble_manager: BLEManager = Depends(get_ble_manager)
):
    """获取所有已连接的设备"""
    try:
        return ble_manager.get_connected_devices()
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/scan", response_model=ScanResponse)
async def scan_devices(
    request: ScanRequest,
    ble_manager: BLEManager = Depends(get_ble_manager)
):
    """扫描附近的BLE设备"""
    try:
        start_time = time.time()
        devices = await ble_manager.scan_devices(
            timeout=request.timeout,
            filter_name=request.filter_name
        )
        scan_duration = time.time() - start_time
        
        return ScanResponse(
            devices=devices,
            scan_duration=scan_duration
        )
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/connect", response_model=DeviceConnection)
async def connect_to_device(
    request: ConnectionRequest,
    ble_manager: BLEManager = Depends(get_ble_manager)
):
    """连接到指定的BLE设备"""
    try:
        connection = await ble_manager.connect_device(
            address=request.address,
            timeout=request.timeout
        )
        return connection
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))

@router.post("/disconnect/{device_address}", response_model=DeviceConnection)
async def disconnect_device(
    device_address: str,
    ble_manager: BLEManager = Depends(get_ble_manager)
):
    """断开与指定设备的连接"""
    try:
        connection = await ble_manager.disconnect_device(device_address)
        return connection
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))

@router.get("/{device_address}/status", response_model=DeviceConnection)
async def get_device_status(
    device_address: str,
    ble_manager: BLEManager = Depends(get_ble_manager)
):
    """获取指定设备的连接状态"""
    try:
        status = ble_manager.get_device_status(device_address)
        if not status:
            raise HTTPException(status_code=404, detail="Device not found")
        return status
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))