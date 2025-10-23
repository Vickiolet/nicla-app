import React from 'react';

function DownloadProgress({ progress }) {
  return (
    <div className="download-progress">
      <h3>下载进度</h3>
      <div className="progress-container">
        <div 
          className="progress-bar" 
          style={{ width: `${progress}%` }}
        >
          {progress.toFixed(1)}%
        </div>
      </div>
    </div>
  );
}

export default DownloadProgress;