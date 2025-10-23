import os
from typing import List, Dict
from .ble_manager import ble_manager

class FileService:
    def __init__(self):
        self.download_progress = {}

    async def list_files(self, device_address: str) -> List[Dict]:
        # 实际应用中需要从设备获取文件列表
        # 这里是示例实现
        if not ble_manager.is_connected():
            return []
        
        # 模拟文件列表
        return [
            {"name": "log_1.txt", "size": 1024, "date": "2023-05-01"},
            {"name": "config.json", "size": 512, "date": "2023-04-28"}
        ]

    async def download_file(self, device_address: str, filename: str, save_path: str) -> Dict:
        # 初始化下载进度
        self.download_progress[filename] = {"progress": 0, "status": "downloading"}
        
        try:
            # 模拟下载过程
            total_size = 1024  # 模拟文件大小
            chunk_size = 100
            
            with open(save_path, 'wb') as f:
                for i in range(0, total_size, chunk_size):
                    # 模拟从设备读取数据
                    await asyncio.sleep(0.1)  # 模拟延迟
                    
                    # 更新进度
                    progress = min(100, int((i / total_size) * 100))
                    self.download_progress[filename]["progress"] = progress
                    
                    # 写入模拟数据
                    f.write(b'0' * min(chunk_size, total_size - i))
            
            self.download_progress[filename]["status"] = "completed"
            return {"success": True, "message": "下载完成"}
        except Exception as e:
            self.download_progress[filename]["status"] = "failed"
            return {"success": False, "message": str(e)}

    def get_download_progress(self, filename: str) -> Dict:
        return self.download_progress.get(filename, {"progress": 0, "status": "not_started"})

# 单例模式
file_service = FileService()