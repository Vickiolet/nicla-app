import axios from 'axios';

// 创建axios实例
const api = axios.create({
  baseURL: process.env.REACT_APP_API_URL || 'http://localhost:8000/api',
  timeout: 10000,
  headers: {
    'Content-Type': 'application/json'
  }
});

// 请求拦截器
api.interceptors.request.use(
  (config) => {
    // 可以在这里添加认证token等
    return config;
  },
  (error) => {
    return Promise.reject(error);
  }
);

// 响应拦截器
api.interceptors.response.use(
  (response) => {
    return response.data;
  },
  (error) => {
    // 统一错误处理
    if (error.response) {
      // 服务器返回错误状态码
      console.error('API Error:', error.response.data);
      return Promise.reject(new Error(error.response.data.detail || '请求失败'));
    } else if (error.request) {
      // 请求发出但没有收到响应
      console.error('Network Error:', error.request);
      return Promise.reject(new Error('网络连接失败，请检查服务器是否正常运行'));
    } else {
      // 请求配置出错
      console.error('Request Config Error:', error.message);
      return Promise.reject(new Error('请求配置错误'));
    }
  }
);

// API方法
const apiService = {
  // 设备相关API
  devices: {
    scan: () => api.get('/devices/scan'),
    connect: (deviceId) => api.post(`/devices/connect/${deviceId}`),
    disconnect: () => api.post('/devices/disconnect'),
    getConnected: () => api.get('/devices/connected')
  },

  // 传感器相关API
  sensors: {
    getConfig: () => api.get('/sensors/config'),
    updateConfig: (config) => api.post('/sensors/config', config),
    startStream: () => api.post('/sensors/stream/start'),
    stopStream: () => api.post('/sensors/stream/stop')
  },

  // 文件相关API
  files: {
    list: () => api.get('/files'),
    download: (fileName) => api.get(`/files/download/${fileName}`, { responseType: 'blob' }),
    upload: (file) => {
      const formData = new FormData();
      formData.append('file', file);
      return api.post('/files/upload', formData, {
        headers: {
          'Content-Type': 'multipart/form-data'
        },
        onUploadProgress: (progressEvent) => {
          const percentCompleted = Math.round((progressEvent.loaded * 100) / progressEvent.total);
          console.log(`Upload Progress: ${percentCompleted}%`);
        }
      });
    },
    delete: (fileName) => api.delete(`/files/${fileName}`),
    updateFirmware: (firmwareFile) => {
      const formData = new FormData();
      formData.append('firmware', firmwareFile);
      return api.post('/files/firmware/update', formData, {
        headers: {
          'Content-Type': 'multipart/form-data'
        }
      });
    }
  },

  // WebSocket连接管理
  createWebSocket: (endpoint, onMessage, onError, onClose) => {
    const wsUrl = process.env.REACT_APP_WS_URL || 'ws://localhost:8000';
    const socket = new WebSocket(`${wsUrl}${endpoint}`);

    socket.onopen = () => {
      console.log(`WebSocket connected: ${endpoint}`);
    };

    socket.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data);
        onMessage(data);
      } catch (error) {
        console.error('Failed to parse WebSocket message:', error);
      }
    };

    socket.onerror = (error) => {
      console.error('WebSocket error:', error);
      if (onError) onError(error);
    };

    socket.onclose = (event) => {
      console.log(`WebSocket closed: ${endpoint}`, event);
      if (onClose) onClose(event);
    };

    return socket;
  },

  // 模式切换
  mode: {
    switch: (mode) => api.post('/mode/switch', { mode })
  }
};

export default apiService;