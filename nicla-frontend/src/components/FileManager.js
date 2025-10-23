import React, { useContext, useState } from 'react';
import { AppContext } from '../context/AppContext';
import DownloadProgress from './DownloadProgress';

function FileManager() {
  const {
    connectedDevice,
    files,
    fetchFiles,
    downloadFile,
    uploadFile,
    deleteFile,
    updateFirmware,
    downloadProgress,
    isDownloading,
    addLog
  } = useContext(AppContext);

  const [selectedFile, setSelectedFile] = useState(null);
  const [uploadingFile, setUploadingFile] = useState(null);
  const [firmwareFile, setFirmwareFile] = useState(null);

  const handleFetchFiles = async () => {
    try {
      addLog('信息', '获取设备文件列表');
      await fetchFiles();
    } catch (error) {
      addLog('错误', `获取文件列表失败: ${error.message}`);
    }
  };

  const handleDownload = async (file) => {
    try {
      addLog('信息', `下载文件: ${file.name}`);
      await downloadFile(file.name);
    } catch (error) {
      addLog('错误', `下载文件失败: ${error.message}`);
    }
  };

  const handleDelete = async (file) => {
    if (window.confirm(`确定要删除文件: ${file.name} 吗？`)) {
      try {
        addLog('信息', `删除文件: ${file.name}`);
        await deleteFile(file.name);
        await fetchFiles(); // 重新获取文件列表
      } catch (error) {
        addLog('错误', `删除文件失败: ${error.message}`);
      }
    }
  };

  const handleUpload = async () => {
    if (!uploadingFile) {
      addLog('警告', '请选择要上传的文件');
      return;
    }

    try {
      addLog('信息', `上传文件: ${uploadingFile.name}`);
      await uploadFile(uploadingFile);
      await fetchFiles(); // 重新获取文件列表
      setUploadingFile(null);
      addLog('成功', '文件上传成功');
    } catch (error) {
      addLog('错误', `文件上传失败: ${error.message}`);
    }
  };

  const handleFirmwareUpdate = async () => {
    if (!firmwareFile) {
      addLog('警告', '请选择固件文件');
      return;
    }

    if (window.confirm('确定要更新固件吗？此操作可能需要几分钟时间，请勿断开连接。')) {
      try {
        addLog('信息', '开始固件更新...');
        await updateFirmware(firmwareFile);
        addLog('成功', '固件更新成功');
      } catch (error) {
        addLog('错误', `固件更新失败: ${error.message}`);
      }
    }
  };

  if (!connectedDevice) {
    return null;
  }

  return (
    <div className="component-card">
      <h2>文件管理器</h2>
      
      <div className="file-actions">
        <button onClick={handleFetchFiles}>刷新文件列表</button>
      </div>
      
      <div className="file-list">
        {files.length === 0 ? (
          <p>暂无文件</p>
        ) : (
          <table>
            <thead>
              <tr>
                <th>文件名</th>
                <th>大小</th>
                <th>修改时间</th>
                <th>操作</th>
              </tr>
            </thead>
            <tbody>
              {files.map((file) => (
                <tr key={file.name}>
                  <td>{file.name}</td>
                  <td>{file.size} 字节</td>
                  <td>{new Date(file.modified).toLocaleString()}</td>
                  <td>
                    <button onClick={() => handleDownload(file)}>下载</button>
                    <button onClick={() => handleDelete(file)}>删除</button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
      
      <div className="file-upload">
        <h3>上传文件</h3>
        <input
          type="file"
          onChange={(e) => setUploadingFile(e.target.files[0])}
        />
        <button onClick={handleUpload} disabled={!uploadingFile}>
          上传文件
        </button>
      </div>
      
      <div className="firmware-update">
        <h3>固件更新</h3>
        <input
          type="file"
          accept=".bin,.hex"
          onChange={(e) => setFirmwareFile(e.target.files[0])}
        />
        <button onClick={handleFirmwareUpdate} disabled={!firmwareFile}>
          更新固件
        </button>
      </div>
      
      {isDownloading && (
        <DownloadProgress progress={downloadProgress} />
      )}
    </div>
  );
}

export default FileManager;