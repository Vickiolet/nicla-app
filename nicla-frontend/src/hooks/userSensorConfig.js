import { useState, useEffect, useCallback } from 'react';
import apiService from '../services/apiService';

function useSensorConfig() {
  const [sensorConfig, setSensorConfig] = useState(null);
  const [isStreaming, setIsStreaming] = useState(false);
  const [sensorData, setSensorData] = useState(null);
  const [wsConnection, setWsConnection] = useState(null);

  // 获取传感器配置
  const getSensorConfig = useCallback(async () => {
    try {
      const config = await apiService.sensors.getConfig();
      setSensorConfig(config);
      return config;
    } catch (error) {
      console.error('获取传感器配置失败:', error);
      throw error;
    }
  }, []);

  // 更新传感器配置
  const updateSensorConfig = async (config) => {
    try {
      const updatedConfig = await apiService.sensors.updateConfig(config);
      setSensorConfig(updatedConfig);
      return updatedConfig;
    } catch (error) {
      throw error;
    }
  };

  // 处理传感器数据
  const handleSensorData = useCallback((data) => {
    setSensorData(data);
  }, []);

  // 处理WebSocket错误
  const handleWebSocketError = useCallback((error) => {
    console.error('WebSocket错误:', error);
    setIsStreaming(false);
    setWsConnection(null);
  }, []);

  // 处理WebSocket关闭
  const handleWebSocketClose = useCallback((event) => {
    console.log('WebSocket关闭:', event);
    setIsStreaming(false);
    setWsConnection(null);
  }, []);

  // 开始传感器数据流
  const startSensorDataStream = async () => {
    try {
      // 先请求后端开始数据流
      await apiService.sensors.startStream();
      
      // 创建WebSocket连接
      const socket = apiService.createWebSocket(
        '/ws/sensor-data',
        handleSensorData,
        handleWebSocketError,
        handleWebSocketClose
      );
      
      setWsConnection(socket);
      setIsStreaming(true);
    } catch (error) {
      throw error;
    }
  };

  // 停止传感器数据流
  const stopSensorDataStream = async () => {
    try {
      // 关闭WebSocket连接
      if (wsConnection) {
        wsConnection.close();
        setWsConnection(null);
      }
      
      // 通知后端停止数据流
      await apiService.sensors.stopStream();
      
      setIsStreaming(false);
    } catch (error) {
      throw error;
    }
  };

  // 组件挂载时获取传感器配置
  useEffect(() => {
    const fetchConfig = async () => {
      try {
        await getSensorConfig();
      } catch (error) {
        console.error('初始化传感器配置失败:', error);
      }
    };
    
    // 可以在这里添加其他初始化逻辑
  }, [getSensorConfig]);

  // 组件卸载时清理WebSocket连接
  useEffect(() => {
    return () => {
      if (wsConnection) {
        wsConnection.close();
      }
    };
  }, [wsConnection]);

  return {
    sensorConfig,
    isStreaming,
    sensorData,
    getSensorConfig,
    updateSensorConfig,
    startSensorDataStream,
    stopSensorDataStream
  };
}

export default useSensorConfig;