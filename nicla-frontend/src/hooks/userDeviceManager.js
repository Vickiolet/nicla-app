import { useState, useEffect, useCallback } from 'react';
import { apiService } from '../services/apiService';
import { useAppContext } from '../context/AppContext';

// 设备管理自定义Hook
export const useDeviceManager = () => {
  const { 
    device, 
    setDevice, 
    isConnected, 
    setIsConnected, 
    setError 
  } = useAppContext();
  
  const [devices, setDevices] = useState([]);
  const [isScanning, setIsScanning] = useState(false);
  const [scanError, setScanError] = useState(null);
  const [connectionError, setConnectionError] = useState(null);
  const [deviceInfo, setDeviceInfo] = useState(null);
  const [connectionStatus, setConnectionStatus] = useState('disconnected'); // disconnected, connecting, connected, failed

  // 扫描设备
  const scanDevices = useCallback(async () => {
    setIsScanning(true);
    setScanError(null);
    try {
      const response = await apiService.scanDevices();
      setDevices(response.devices);
      return response.devices;
    } catch (err) {
      setScanError(err.message);
      setError(`扫描设备失败: ${err.message}`);
      throw err;
    } finally {
      setIsScanning(false);import { useState, useEffect, useCallback } from 'react';
import apiService from '../services/apiService';

function useDeviceManager() {
  const [devices, setDevices] = useState([]);
  const [connectedDevice, setConnectedDevice] = useState(null);
  const [isScanning, setIsScanning] = useState(false);
  const [files, setFiles] = useState([]);
  const [downloadProgress, setDownloadProgress] = useState(0);
  const [isDownloading, setIsDownloading] = useState(false);
  const [scanInterval, setScanInterval] = useState(null);

  // 开始扫描设备
  const startScan = useCallback(() => {
    setIsScanning(true);
    
    // 立即扫描一次
    scanDevices();
    
    // 设置定期扫描
    const interval = setInterval(() => {
      scanDevices();
    }, 5000); // 每5秒扫描一次
    
    setScanInterval(interval);
  }, []);

  // 扫描设备
  const scanDevices = async () => {
    try {
      const deviceList = await apiService.devices.scan();
      setDevices(deviceList);
    } catch (error) {
      console.error('扫描设备失败:', error);
      // 不设置全局错误，避免影响用户体验
    }
  };

  // 停止扫描
  const stopScan = useCallback(() => {
    setIsScanning(false);
    if (scanInterval) {
      clearInterval(scanInterval);
      setScanInterval(null);
    }
  }, [scanInterval]);

  // 连接设备
  const connectToDevice = async (deviceId) => {
    try {
      const device = await apiService.devices.connect(deviceId);
      setConnectedDevice(device);
      // 连接成功后获取文件列表
      fetchFiles();
      return device;
    } catch (error) {
      throw error;
    }
  };

  // 断开连接
  const disconnectFromDevice = async () => {
    try {
      await apiService.devices.disconnect();
      setConnectedDevice(null);
      setFiles([]);
    } catch (error) {
      throw error;
    }
  };

  // 获取文件列表
  const fetchFiles = async () => {
    try {
      const fileList = await apiService.files.list();
      setFiles(fileList);
    } catch (error) {
      throw error;
    }
  };

  // 下载文件
  const downloadFile = async (fileName) => {
    try {
      setIsDownloading(true);
      setDownloadProgress(0);
      
      const response = await apiService.files.download(fileName);
      
      // 创建下载链接
      const url = window.URL.createObjectURL(new Blob([response]));
      const link = document.createElement('a');
      link.href = url;
      link.setAttribute('download', fileName);
      document.body.appendChild(link);
      link.click();
      
      // 清理
      link.parentNode.removeChild(link);
      setDownloadProgress(100);
      
      setTimeout(() => {
        setIsDownloading(false);
        setDownloadProgress(0);
      }, 1000);
      
    } catch (error) {
      setIsDownloading(false);
      setDownloadProgress(0);
      throw error;
    }
  };

  // 上传文件
  const uploadFile = async (file) => {
    try {
      return await apiService.files.upload(file);
    } catch (error) {
      throw error;
    }
  };

  // 删除文件
  const deleteFile = async (fileName) => {
    try {
      return await apiService.files.delete(fileName);
    } catch (error) {
      throw error;
    }
  };

  // 更新固件
  const updateFirmware = async (firmwareFile) => {
    try {
      return await apiService.files.updateFirmware(firmwareFile);
    } catch (error) {
      throw error;
    }
  };

  // 检查连接状态
  const checkConnectionStatus = async () => {
    try {
      const device = await apiService.devices.getConnected();
      if (device) {
        setConnectedDevice(device);
        fetchFiles();
      }
    } catch (error) {
      console.error('检查连接状态失败:', error);
    }
  };

  // 组件挂载时检查连接状态
  useEffect(() => {
    checkConnectionStatus();
  }, []);

  // 组件卸载时清理
  useEffect(() => {
    return () => {
      stopScan();
    };
  }, [stopScan]);

  return {
    devices,
    connectedDevice,
    isScanning,
    files,
    downloadProgress,
    isDownloading,
    startScan,
    stopScan,
    connectToDevice,
    disconnectFromDevice,
    fetchFiles,
    downloadFile,
    uploadFile,
    deleteFile,
    updateFirmware
  };
}

export default useDeviceManager;