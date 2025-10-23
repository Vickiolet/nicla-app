// 格式化工具函数

// 格式化日期时间
export const formatDateTime = (date) => {
  if (!date) return '';
  const d = new Date(date);
  return d.toLocaleString('zh-CN', {
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit'
  });
};

// 格式化文件大小
export const formatFileSize = (bytes) => {
  if (bytes === 0) return '0 Bytes';
  const k = 1024;
  const sizes = ['Bytes', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
};

// 格式化传感器数据
export const formatSensorData = (data) => {
  if (!data) return null;
  
  const formattedData = {};
  
  // 格式化加速度数据
  if (data.accelerometer) {
    formattedData.accelerometer = {
      x: data.accelerometer.x.toFixed(3),
      y: data.accelerometer.y.toFixed(3),
      z: data.accelerometer.z.toFixed(3),
      unit: 'g'
    };
  }
  
  // 格式化陀螺仪数据
  if (data.gyroscope) {
    formattedData.gyroscope = {
      x: data.gyroscope.x.toFixed(3),
      y: data.gyroscope.y.toFixed(3),
      z: data.gyroscope.z.toFixed(3),
      unit: '°/s'
    };
  }
  
  // 格式化磁力计数据
  if (data.magnetometer) {
    formattedData.magnetometer = {
      x: data.magnetometer.x.toFixed(3),
      y: data.magnetometer.y.toFixed(3),
      z: data.magnetometer.z.toFixed(3),
      unit: 'μT'
    };
  }
  
  // 格式化环境数据
  if (data.temperature) {
    formattedData.temperature = {
      value: data.temperature.toFixed(2),
      unit: '°C'
    };
  }
  
  if (data.humidity) {
    formattedData.humidity = {
      value: data.humidity.toFixed(2),
      unit: '%'
    };
  }
  
  if (data.pressure) {
    formattedData.pressure = {
      value: data.pressure.toFixed(2),
      unit: 'hPa'
    };
  }
  
  // 添加时间戳
  formattedData.timestamp = formatDateTime(data.timestamp || new Date());
  
  return formattedData;
};

// 验证设备ID
export const isValidDeviceId = (deviceId) => {
  return deviceId && typeof deviceId === 'string' && deviceId.length > 0;
};

// 验证传感器配置
export const validateSensorConfig = (config) => {
  if (!config || typeof config !== 'object') {
    return { valid: false, error: '无效的配置对象' };
  }
  
  // 基本验证逻辑
  const sensorTypes = ['accelerometer', 'gyroscope', 'magnetometer', 'temperature', 'humidity', 'pressure'];
  
  for (const [key, sensorConfig] of Object.entries(config)) {
    if (!sensorTypes.includes(key)) {
      return { valid: false, error: `未知的传感器类型: ${key}` };
    }
    
    if (typeof sensorConfig.enabled !== 'boolean') {
      return { valid: false, error: `传感器 ${key} 的enabled属性必须是布尔值` };
    }
    
    if (typeof sensorConfig.samplingRate !== 'number' || sensorConfig.samplingRate < 1) {
      return { valid: false, error: `传感器 ${key} 的samplingRate必须是大于0的数字` };
    }
    
    // 验证范围值
    if (['accelerometer', 'gyroscope', 'magnetometer'].includes(key)) {
      if (typeof sensorConfig.range !== 'number' || sensorConfig.range <= 0) {
        return { valid: false, error: `传感器 ${key} 的range必须是大于0的数字` };
      }
    }
  }
  
  return { valid: true };
};

// 生成唯一ID
export const generateId = () => {
  return Date.now().toString(36) + Math.random().toString(36).substr(2);
};

// 防抖函数
export const debounce = (func, wait) => {
  let timeout;
  return function executedFunction(...args) {
    const later = () => {
      clearTimeout(timeout);
      func(...args);
    };
    clearTimeout(timeout);
    timeout = setTimeout(later, wait);
  };
};

// 节流函数
export const throttle = (func, limit) => {
  let inThrottle;
  return function(...args) {
    const context = this;
    if (!inThrottle) {
      func.apply(context, args);
      inThrottle = true;
      setTimeout(() => inThrottle = false, limit);
    }
  };
};