import React, { useState, useEffect } from 'react';
import { useAppContext } from '../context/AppContext';
import { apiService } from '../services/apiService';
import { useNavigate } from 'react-router-dom';
import { 
  Paper, Typography, Box, Button, FormControl, Select, MenuItem, 
  InputLabel, Switch, FormControlLabel, List, ListItem, Chip, 
  Divider, CircularProgress, Alert
} from '@mui/material';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from 'recharts';
import { Settings, PlayCircle, PauseCircle, Download, RefreshCw } from '@mui/icons-material';

const SensorConfig = () => {
  const { device, isConnected } = useAppContext();
  const [sensorTypes, setSensorTypes] = useState([]);
  const [selectedSensor, setSelectedSensor] = useState('');
  const [sensors, setSensors] = useState([]);
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState(null);
  const [isStreaming, setIsStreaming] = useState(false);
  const [streamData, setStreamData] = useState([]);
  const [config, setConfig] = useState({
    sampleRate: 10,
    range: 2,
    enabled: false
  });
  
  const navigate = useNavigate();
  let ws = null;

  useEffect(() => {
    if (!isConnected || !device) {
      navigate('/');
      return;
    }
    
    fetchSensorInfo();
    fetchSensorTypes();
  }, [isConnected, device, navigate]);

  const fetchSensorInfo = async () => {
    setIsLoading(true);
    setError(null);
    try {
      const response = await apiService.getSensors();
      setSensors(response.sensors);
    } catch (err) {
      setError('获取传感器信息失败: ' + err.message);
      console.error('获取传感器信息错误:', err);
    } finally {
      setIsLoading(false);
    }
  };

  const fetchSensorTypes = async () => {
    try {
      const response = await apiService.getSensorTypes();
      setSensorTypes(response.types);
    } catch (err) {
      console.error('获取传感器类型错误:', err);
    }
  };

  const handleSensorChange = (e) => {
    const sensorId = e.target.value;
    setSelectedSensor(sensorId);
    
    // 查找该传感器的当前配置
    const sensor = sensors.find(s => s.id === sensorId);
    if (sensor && sensor.config) {
      setConfig(sensor.config);
    } else {
      setConfig({ sampleRate: 10, range: 2, enabled: false });
    }
  };

  const updateSensorConfig = async () => {
    if (!selectedSensor) return;
    
    setError(null);
    try {
      await apiService.updateSensorConfig(selectedSensor, config);
      // 更新本地传感器列表
      const updatedSensors = sensors.map(s => 
        s.id === selectedSensor ? { ...s, config } : s
      );
      setSensors(updatedSensors);
    } catch (err) {
      setError('更新传感器配置失败: ' + err.message);
      console.error('更新传感器配置错误:', err);
    }
  };

  const startDataStream = async () => {
    if (!selectedSensor) {
      setError('请先选择一个传感器');
      return;
    }
    
    setError(null);
    try {
      // 确保传感器已启用
      if (!config.enabled) {
        const updatedConfig = { ...config, enabled: true };
        setConfig(updatedConfig);
        await apiService.updateSensorConfig(selectedSensor, updatedConfig);
      }
      
      // 建立WebSocket连接
      const wsUrl = `ws://localhost:8000/api/sensors/${selectedSensor}/stream`;
      ws = new WebSocket(wsUrl);
      
      ws.onopen = () => {
        console.log('WebSocket连接已建立');
        setIsStreaming(true);
        setStreamData([]);
      };
      
      ws.onmessage = (event) => {
        const data = JSON.parse(event.data);
        setStreamData(prev => {
          const newData = [...prev, { timestamp: Date.now(), value: data.value }];
          // 只保留最近100个数据点
          return newData.slice(-100);
        });
      };
      
      ws.onclose = () => {
        console.log('WebSocket连接已关闭');
        setIsStreaming(false);
      };
      
      ws.onerror = (error) => {
        console.error('WebSocket错误:', error);
        setError('数据流连接失败');
        setIsStreaming(false);
      };
    } catch (err) {
      setError('启动数据流失败: ' + err.message);
      console.error('启动数据流错误:', err);
    }
  };

  const stopDataStream = () => {
    if (ws) {
      ws.close();
      ws = null;
    }
    setIsStreaming(false);
  };

  // 清理WebSocket连接
  useEffect(() => {
    return () => {
      if (ws) {
        ws.close();
      }
    };
  }, []);

  return (
    <Paper elevation={3} className="card">
      <Box className="card-header">
        <Typography variant="h5" component="h2" className="card-title">
          传感器配置
        </Typography>
        <Typography variant="body2" color="text.secondary">
          连接设备: {device?.name || device?.address}
        </Typography>
      </Box>
      
      {error && (
        <Alert severity="error" sx={{ mb: 2 }}>
          {error}
        </Alert>
      )}
      
      <Box sx={{ mb: 3, display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <Button
          variant="outlined"
          color="primary"
          startIcon={<RefreshCw />}
          onClick={fetchSensorInfo}
          disabled={isLoading}
        >
          刷新传感器列表
        </Button>
        <Button
          variant="contained"
          color="primary"
          startIcon={<Download />}
          onClick={() => navigate('/files')}
        >
          文件管理
        </Button>
      </Box>
      
      {isLoading ? (
        <Box className="loading">
          <CircularProgress />
          <Typography variant="body1" sx={{ ml: 2 }}>加载传感器信息...</Typography>
        </Box>
      ) : (
        <Box>
          <FormControl fullWidth sx={{ mb: 3 }}>
            <InputLabel id="sensor-select-label">选择传感器</InputLabel>
            <Select
              labelId="sensor-select-label"
              id="sensor-select"
              value={selectedSensor}
              label="选择传感器"
              onChange={handleSensorChange}
            >
              {sensors.map((sensor) => (
                <MenuItem key={sensor.id} value={sensor.id}>
                  {sensor.name} ({sensor.type})
                </MenuItem>
              ))}
            </Select>
          </FormControl>
          
          {selectedSensor && (
            <Box>
              <Typography variant="h6" sx={{ mb: 2 }}>传感器配置</Typography>
              
              <Box sx={{ display: 'grid', gap: 2, gridTemplateColumns: { xs: '1fr', md: '1fr 1fr' } }}>
                <FormControl fullWidth>
                  <InputLabel id="sample-rate-label">采样率 (Hz)</InputLabel>
                  <Select
                    labelId="sample-rate-label"
                    id="sample-rate"
                    value={config.sampleRate}
                    label="采样率 (Hz)"
                    onChange={(e) => setConfig({ ...config, sampleRate: e.target.value })}
                  >
                    <MenuItem value={1}>1</MenuItem>
                    <MenuItem value={5}>5</MenuItem>
                    <MenuItem value={10}>10</MenuItem>
                    <MenuItem value={20}>20</MenuItem>
                    <MenuItem value={50}>50</MenuItem>
                    <MenuItem value={100}>100</MenuItem>
                  </Select>
                </FormControl>
                
                <FormControl fullWidth>
                  <InputLabel id="range-label">量程</InputLabel>
                  <Select
                    labelId="range-label"
                    id="range"
                    value={config.range}
                    label="量程"
                    onChange={(e) => setConfig({ ...config, range: e.target.value })}
                  >
                    <MenuItem value={1}>±1</MenuItem>
                    <MenuItem value={2}>±2</MenuItem>
                    <MenuItem value={4}>±4</MenuItem>
                    <MenuItem value={8}>±8</MenuItem>
                    <MenuItem value={16}>±16</MenuItem>
                  </Select>
                </FormControl>
              </Box>
              
              <Box sx={{ mt: 2, mb: 3 }}>
                <FormControlLabel
                  control={
                    <Switch
                      checked={config.enabled}
                      onChange={(e) => setConfig({ ...config, enabled: e.target.checked })}
                      color="primary"
                    />
                  }
                  label="启用传感器"
                />
              </Box>
              
              <Button
                variant="contained"
                color="primary"
                startIcon={<Settings />}
                onClick={updateSensorConfig}
                sx={{ mb: 4 }}
              >
                应用配置
              </Button>
              
              <Divider sx={{ mb: 4 }} />
              
              <Typography variant="h6" sx={{ mb: 2 }}>实时数据流</Typography>
              
              <Box sx={{ display: 'flex', gap: 2, mb: 3 }}>
                <Button
                  variant="contained"
                  color={isStreaming ? "error" : "success"}
                  startIcon={isStreaming ? <PauseCircle /> : <PlayCircle />}
                  onClick={isStreaming ? stopDataStream : startDataStream}
                >
                  {isStreaming ? '停止数据流' : '开始数据流'}
                </Button>
              </Box>
              
              <Box sx={{ height: 300 }}>
                {isStreaming ? (
                  <ResponsiveContainer width="100%" height="100%">
                    <LineChart data={streamData} margin={{ top: 5, right: 20, bottom: 5, left: 0 }}>
                      <CartesianGrid strokeDasharray="3 3" stroke="#f0f0f0" />
                      <XAxis dataKey="timestamp" hide />
                      <YAxis />
                      <Tooltip 
                        formatter={(value) => [value.toFixed(4), '值']}
                        labelFormatter={(timestamp) => new Date(timestamp).toLocaleTimeString()}
                      />
                      <Line 
                        type="monotone" 
                        dataKey="value" 
                        stroke="#1976d2" 
                        strokeWidth={2} 
                        dot={false} 
                        activeDot={{ r: 6 }}
                      />
                    </LineChart>
                  </ResponsiveContainer>
                ) : (
                  <Box className="loading" sx={{ height: '100%' }}>
                    <Typography variant="body1" color="text.secondary">
                      点击"开始数据流"按钮查看实时数据
                    </Typography>
                  </Box>
                )}
              </Box>
            </Box>
          )}
          
          <Box sx={{ mt: 4 }}>
            <Typography variant="h6" sx={{ mb: 2 }}>传感器信息</Typography>
            <List>
              {sensors.map((sensor) => (
                <ListItem key={sensor.id} sx={{ borderBottom: '1px solid #eee' }}>
                  <Box sx={{ flex: 1 }}>
                    <Typography variant="subtitle1">{sensor.name}</Typography>
                    <Typography variant="body2" color="text.secondary">
                      类型: {sensor.type} | ID: {sensor.id}
                    </Typography>
                  </Box>
                  <Chip 
                    label={sensor.config?.enabled ? "已启用" : "已禁用"} 
                    color={sensor.config?.enabled ? "success" : "default"}
                    size="small"
                  />
                </ListItem>
              ))}
            </List>
          </Box>
        </Box>
      )}
    </Paper>
  );
};

export default SensorConfig;