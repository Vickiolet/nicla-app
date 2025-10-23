import React, { useContext, useRef, useEffect } from 'react';
import { AppContext } from '../context/AppContext';

function LogViewer() {
  const { logs } = useContext(AppContext);
  const logContainerRef = useRef(null);

  // 自动滚动到最新日志
  useEffect(() => {
    if (logContainerRef.current) {
      logContainerRef.current.scrollTop = logContainerRef.current.scrollHeight;
    }
  }, [logs]);

  const getLogClass = (type) => {
    switch (type.toLowerCase()) {
      case 'error':
        return 'log-error';
      case 'success':
        return 'log-success';
      case 'info':
        return 'log-info';
      default:
        return '';
    }
  };

  return (
    <div className="component-card">
      <h2>日志查看器</h2>
      <div className="log-viewer" ref={logContainerRef}>
        {logs.length === 0 ? (
          <div className="log-entry">暂无日志</div>
        ) : (
          logs.map((log, index) => (
            <div 
              key={index} 
              className={`log-entry ${getLogClass(log.type)}`}
            >
              [{new Date(log.timestamp).toLocaleTimeString()}] [{log.type}] {log.message}
            </div>
          ))
        )}
      </div>
    </div>
  );
}

export default LogViewer;