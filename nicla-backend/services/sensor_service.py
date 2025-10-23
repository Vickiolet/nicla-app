from typing import Dict, Any
from .ble_manager import ble_manager

class SensorService:
    def __init__(self):
        self.configured_sensors = {}

    async def configure_sensor(self, sensor_id: int, config: Dict[str, Any]) -> bool:
        # 检查设备连接状态
        if not ble_manager.is_connected():
            return False

        try:
            # 这里是示例实现，实际需要根据设备协议打包数据并发送
            # 假设使用特定的UUID进行通信
            characteristic_uuid = "YOUR_SENSOR_CONFIG_CHARACTERISTIC_UUID"
            
            # 构建配置数据（示例）
            sample_rate = config.get("sample_rate", 10)
            range_value = config.get("range", 2)
            
            # 实际应用中需要按照设备要求的格式打包数据
            data = self._pack_config_data(sensor_id, sample_rate, range_value)
            
            # 发送配置数据
            await ble_manager.client.write_gatt_char(characteristic_uuid, data)
            
            # 保存已配置的传感器信息
            self.configured_sensors[sensor_id] = config
            return True
        except Exception as e:
            print(f"配置传感器失败: {e}")
            return False

    def _pack_config_data(self, sensor_id: int, sample_rate: int, range_value: int) -> bytes:
        # 这里需要根据实际的协议格式来实现数据打包
        # 以下是示例实现
        import struct
        return struct.pack('<BHH', sensor_id, sample_rate, range_value)

    def get_configured_sensors(self) -> Dict[int, Dict[str, Any]]:
        return self.configured_sensors

# 单例模式
sensor_service = SensorService()