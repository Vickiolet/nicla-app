from fastapi import FastAPI, HTTPException, BackgroundTasks
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from typing import List, Dict, Optional
import uvicorn
import asyncio

# 导入服务
from services.ble_manager import ble_manager
from services.sensor_service import sensor_service
from services.file_service import file_service

app = FastAPI(title="Nicla Sense Controller API")

# 允许CORS
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # 在生产环境中应限制为特定的源
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Pydantic模型
class SensorConfig(BaseModel):
    sample_rate: int
    range: int
    enabled: bool = True

class DeviceCommand(BaseModel):
    address: str
    command: str
    params: Optional[Dict] = None

class FileDownloadRequest(BaseModel):
    filename: str
    save_path: str

# API路由
@app.get("/api/health", tags=["基础"])
async def health_check():
    return {"status": "ok", "message": "服务正常运行"}

@app.get("/api/devices", tags=["设备管理"])
async def get_devices():
    try:
        devices = await ble_manager.scan_devices()
        return {"success": True, "devices": devices}
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"扫描设备失败: {str(e)}")

@app.post("/api/device/connect", tags=["设备管理"])
async def connect_device(address: str):
    try:
        success = await ble_manager.connect(address)
        if success:
            return {"success": True, "message": "设备连接成功"}
        else:
            raise HTTPException(status_code=400, detail="设备连接失败")
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"连接设备时发生错误: {str(e)}")

@app.post("/api/device/disconnect", tags=["设备管理"])
async def disconnect_device():
    try:
        success = await ble_manager.disconnect()
        if success:
            return {"success": True, "message": "设备断开成功"}
        else:
            raise HTTPException(status_code=400, detail="设备断开失败")
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"断开设备时发生错误: {str(e)}")

@app.get("/api/device/connection", tags=["设备管理"])
async def get_connection_status():
    return {
        "connected": ble_manager.is_connected(),
        "device": ble_manager.connected_device
    }

@app.post("/api/sensor/configure/{sensor_id}", tags=["传感器管理"])
async def configure_sensor(sensor_id: int, config: SensorConfig):
    try:
        success = await sensor_service.configure_sensor(sensor_id, config.dict())
        if success:
            return {"success": True, "message": f"传感器 {sensor_id} 配置成功"}
        else:
            raise HTTPException(status_code=400, detail=f"传感器 {sensor_id} 配置失败")
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"配置传感器时发生错误: {str(e)}")

@app.get("/api/sensor/configured", tags=["传感器管理"])
async def get_configured_sensors():
    return {"sensors": sensor_service.get_configured_sensors()}

@app.get("/api/files", tags=["文件管理"])
async def get_files():
    try:
        if not ble_manager.is_connected():
            raise HTTPException(status_code=400, detail="设备未连接")
        
        files = await file_service.list_files(ble_manager.connected_device)
        return {"success": True, "files": files}
    except Exception as e:
        if isinstance(e, HTTPException):
            raise e
        raise HTTPException(status_code=500, detail=f"获取文件列表失败: {str(e)}")

@app.post("/api/file/download", tags=["文件管理"])
async def download_file(request: FileDownloadRequest, background_tasks: BackgroundTasks):
    try:
        if not ble_manager.is_connected():
            raise HTTPException(status_code=400, detail="设备未连接")
        
        # 使用后台任务处理文件下载
        background_tasks.add_task(
            file_service.download_file,
            ble_manager.connected_device,
            request.filename,
            request.save_path
        )
        
        return {"success": True, "message": "下载任务已开始"}
    except Exception as e:
        if isinstance(e, HTTPException):
            raise e
        raise HTTPException(status_code=500, detail=f"启动文件下载失败: {str(e)}")

@app.get("/api/file/progress/{filename}", tags=["文件管理"])
async def get_download_progress(filename: str):
    return file_service.get_download_progress(filename)

if __name__ == "__main__":
    uvicorn.run("main:app", host="0.0.0.0", port=8000, reload=True)