import React, { useState, useEffect } from 'react';
import { useAppContext } from '../context/AppContext';
import { apiService } from '../services/apiService';
import { useNavigate } from 'react-router-dom';
import { CircularProgress, Button, List, ListItem, ListItemText, Paper, Typography, Box, Alert } from '@mui/material';
import { Search, Refresh, Bluetooth, CheckCircle } from '@mui/icons-material';

const DeviceSelector = () => {
  const { device, setDevice, isConnected, setIsConnected } = useAppContext();
  const [devices, setDevices] = useState([]);
  const [isScanning, setIsScanning] = useState(false);
  const [error, setError] = useState(null);
  const navigate = useNavigate();

  const scanDevices = async () => {
    setIsScanning(true);
    setError(null);
    try {
      const response = await apiService.scanDevices();
      setDevices(response.devices);
    } catch (err) {
      setError('扫描设备失败: ' + err.message);
      console.error('扫描设备错误:', err);
    } finally {
      setIsScanning(false);
    }
  };

  const connectToDevice = async (deviceId) => {
    setError(null);
    try {
      const response = await apiService.connectDevice(deviceId);
      setDevice(response.device);
      setIsConnected(true);
      navigate('/sensors');
    } catch (err) {
      setError('连接设备失败: ' + err.message);
      console.error('连接设备错误:', err);
    }
  };

  useEffect(() => {
    scanDevices();
    // 清理函数
    return () => {
      // 如果组件卸载时正在扫描，可以在这里取消扫描
    };
  }, []);

  return (
    <Paper elevation={3} className="card">
      <Box className="card-header">
        <Typography variant="h5" component="h2" className="card-title">
          选择设备
        </Typography>
      </Box>
      
      {error && (
        <Alert severity="error" sx={{ mb: 2 }}>
          {error}
        </Alert>
      )}
      
      <Box sx={{ display: 'flex', gap: 2, mb: 3 }}>
        <Button
          variant="contained"
          color="primary"
          startIcon={<Refresh />}
          onClick={scanDevices}
          disabled={isScanning}
          sx={{ flex: 1 }}
        >
          {isScanning ? '扫描中...' : '扫描设备'}
        </Button>
        <Button
          variant="outlined"
          color="primary"
          startIcon={<Bluetooth />}
          onClick={() => console.log('蓝牙设置')}
          sx={{ flex: 1 }}
        >
          蓝牙设置
        </Button>
      </Box>
      
      {isScanning ? (
        <Box className="loading">
          <CircularProgress />
          <Typography variant="body1" sx={{ ml: 2 }}>正在搜索设备...</Typography>
        </Box>
      ) : (
        <List sx={{ maxHeight: 400, overflowY: 'auto' }}>
          {devices.length === 0 ? (
            <ListItem>
              <ListItemText 
                primary="未发现设备"
                secondary="请确保蓝牙已开启，设备处于可连接状态"
              />
            </ListItem>
          ) : (
            devices.map((device) => (
              <ListItem 
                button 
                key={device.id} 
                onClick={() => connectToDevice(device.id)}
                sx={{ 
                  '&:hover': { backgroundColor: 'rgba(0,0,0,0.04)' },
                  cursor: 'pointer',
                  borderRadius: 1,
                  mb: 1
                }}
              >
                <Bluetooth sx={{ mr: 2, color: '#1976d2' }} />
                <ListItemText 
                  primary={device.name || '未知设备'}
                  secondary={device.address}
                />
                {device.id === (device?.id || '') && (
                  <CheckCircle size={20} color="success" />
                )}
              </ListItem>
            ))
          )}
        </List>
      )}
      
      {isConnected && device && (
        <Box sx={{ mt: 3, p: 2, bgcolor: 'background.default', borderRadius: 1 }}>
          <Typography variant="body2" color="text.secondary">
            当前已连接设备: {device.name || device.address}
          </Typography>
        </Box>
      )}
    </Paper>
  );
};

export default DeviceSelector;