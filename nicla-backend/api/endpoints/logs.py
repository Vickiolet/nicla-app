from fastapi import APIRouter, HTTPException
from pydantic import BaseModel
from typing import List, Dict
from services.ble_logger import ble_logger
import asyncio

router = APIRouter(
    prefix="/logs",
    tags=["logs"],
    responses={404: {"description": "Not found"}},
)

# ---------- 1. 扫描附近BLE设备 ----------
@router.get("/scan", response_model=List[Dict])
async def scan_ble_devices():
    """
    扫描附近的BLE设备，返回[{name,address}, ...]
    """
    try:
        devices = await ble_logger.scan_devices()
        return devices
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"扫描设备失败: {str(e)}")


# ---------- 2. 连接并进入配置模式 ----------
@router.post("/connect/{device_address}")
async def connect_device_and_enter_config(device_address: str):
    """
    连接到指定BLE设备并让其进入配置模式（CONFIG）。
    连接会被保存在后端的 ble_logger.connected 里，供后续请求复用。
    """
    try:
        result = await ble_logger.connect_and_enter_config(device_address)
        if not result.get("success"):
            raise HTTPException(status_code=500, detail=result.get("message", "连接失败"))
        return result
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"连接/进入配置模式失败: {str(e)}")


# ---------- 3. 仅列出远程(板子上)的日志文件 ----------
@router.get("/remote-files/{device_address}")
async def list_remote_files_on_device(device_address: str):
    """
    要求设备已经通过 /connect 连接并进入配置模式。
    向板子发送 LIST，不下载，只返回板子上的日志文件列表。
    """
    try:
        result = await ble_logger.list_remote_files(device_address)
        if not result.get("success"):
            raise HTTPException(status_code=500, detail=result.get("message", "LIST失败"))
        return result
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"获取远程文件列表失败: {str(e)}")


# ---------- 4. 选择性拉取指定文件 ----------
class PullSelectedBody(BaseModel):
    files: List[str]

@router.post("/pull-selected/{device_address}")
async def pull_selected_logs(device_address: str, body: PullSelectedBody):
    """
    对已经连接并处于配置模式的设备，只拉取 body.files 中指定的那些文件。
    不会自动断开BLE，也不会恢复采样。
    前端可以多次调用本接口，分批拉。
    """
    try:
        result = await ble_logger.pull_selected_files(device_address, body.files)
        if not result.get("success"):
            raise HTTPException(status_code=500, detail=result.get("message", "拉取部分文件失败"))
        return result
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"拉取所选文件失败: {str(e)}")


# ---------- 5. 结束：恢复采样并断开 ----------
@router.post("/finalize/{device_address}")
@router.post("/finalize/{device_address}")
async def finalize_and_disconnect(device_address: str):
    """
    让板子恢复正常LOG采样(STARTLOG)，然后断开BLE连接并清理状态。
    幂等：即使连接早就断了，这里也会返回 success=True，
    表示「后端已经不再持有这个设备的会话了」。
    """
    try:
        result = await ble_logger.finalize_and_disconnect(device_address)
        # finalize_and_disconnect 现在保证永远 success=True
        return result
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"finalize失败: {str(e)}")


# ---------- 6. 兼容旧的一键全量拉取 ----------
@router.post("/pull/{device_address}")
async def pull_device_logs(device_address: str):
    """
    旧模式：一键执行
    1. LIST
    2. GET 全部
    3. ACK
    4. STARTLOG 恢复采样
    5. 断开 BLE
    新实现内部会调用 ble_logger.pull_logs_from_device()。
    """
    try:
        result = await ble_logger.pull_logs_from_device(device_address)
        if not result.get("success"):
            raise HTTPException(status_code=500, detail=result.get("message", "拉取失败"))
        return result
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"拉取日志失败: {str(e)}")


# ---------- 7. 查看后端本地已有的日志文件 ----------
@router.get("/files", response_model=List[str])
async def list_pulled_log_files():
    """
    列出后端已经保存下来的日志文件(通常是 .csv)。
    这些文件位于后端服务器的 pulled_logs/ 目录。
    """
    return ble_logger.get_pulled_logs()


# ---------- 8. 查看某个本地日志文件的内容 ----------
@router.get("/file/{filename}")
async def get_log_file_content(filename: str):
    """
    返回本地 pulled_logs/<filename> 的文本内容（一般是 CSV）。
    前端可以用来展示折线图/表格等可视化。
    """
    try:
        content = ble_logger.read_log_file(filename)
        return {
            "filename": filename,
            "content": content
        }
    except FileNotFoundError as e:
        raise HTTPException(status_code=404, detail=str(e))
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"读取文件失败: {str(e)}")
