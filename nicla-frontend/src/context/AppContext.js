import React, { createContext, useState, useEffect } from 'react';
import apiService from '../services/apiService';
import useDeviceManager from '../hooks/useDeviceManager';
import useSensorConfig from '../hooks/useSensorConfig';

// 创建Context
export const AppContext = createContext();

// Context Provider组件
function AppContextProvider({ children }) {
  // 基础状态
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState(null);
  const [logs, setLogs] = useState([]);
  const [currentMode, setCurrentMode] = useState('正常模式');
  const [availableModes] = useState(['正常模式', '调试模式', '低功耗模式']);

  // 使用自定义钩子
  const deviceManager = useDeviceManager();
  const sensorManager = useSensorConfig();

  // 添加日志
  const addLog = (type, message) => {
    const newLog = {
      type,
      message,
      timestamp: new Date().toISOString()
    };
    setLogs(prev => [...prev, newLog].slice(-100)); // 保留最近100条日志
  };

  // 切换模式
  const switchMode = async (mode) => {
    try {
      setIsLoading(true);
      await apiService.mode.switch(mode);
      setCurrentMode(mode);
    } catch (err) {
      setError(err.message);
      addLog('错误', `模式切换失败: ${err.message}`);
    } finally {
      setIsLoading(false);
    }
  };

  // 清除错误
  const clearError = () => {
    setError(null);
  };

  // 组合所有状态和方法
  const contextValue = {
    // 基础状态
    isLoading,
    error,
    clearError,
    logs,
    addLog,
    currentMode,
    switchMode,
    availableModes,
    
    // 设备管理
    ...deviceManager,
    
    // 传感器管理
    ...sensorManager
  };

  // 组件挂载时添加初始日志
  useEffect(() => {
    addLog('信息', '应用已启动');
  }, []);

  return (
    <AppContext.Provider value={contextValue}>
      {children}
    </AppContext.Provider>
  );
}

export default AppContextProvider;