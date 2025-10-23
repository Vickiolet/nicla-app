from bleak import BleakScanner, BleakClient
import asyncio
from typing import List, Dict, Optional

class BLEManager:
    def __init__(self):
        self.client = None
        self.connected_device = None

    async def scan_devices(self) -> List[Dict]:
        devices = await BleakScanner.discover()
        return [{
            "name": device.name if device.name else "Unknown",
            "address": device.address,
            "rssi": device.rssi
        } for device in devices]

    async def connect(self, address: str) -> bool:
        try:
            self.client = BleakClient(address)
            await self.client.connect()
            self.connected_device = address
            return True
        except Exception as e:
            print(f"连接失败: {e}")
            return False

    async def disconnect(self) -> bool:
        try:
            if self.client and self.client.is_connected:
                await self.client.disconnect()
                self.connected_device = None
                return True
            return False
        except Exception as e:
            print(f"断开连接失败: {e}")
            return False

    def is_connected(self) -> bool:
        return self.client is not None and self.client.is_connected

# 单例模式
ble_manager = BLEManager()