import React from 'react';
import { Routes, Route } from 'react-router-dom';
import DeviceSelector from './components/DeviceSelector';
import SensorConfig from './components/SensorConfig';
import FileManager from './components/FileManager';
import LogViewer from './components/LogViewer';
import ModeSwitcher from './components/ModeSwitcher';
import './App.css';

function App() {
  return (
    <div className="App">
      <header className="App-header">
        <h1>Nicla Sense ME 控制器</h1>
        <ModeSwitcher />
      </header>
      <main className="App-main">
        <Routes>
          <Route path="/" element={<DeviceSelector />} />
          <Route path="/sensors" element={<SensorConfig />} />
          <Route path="/files" element={<FileManager />} />
          <Route path="/logs" element={<LogViewer />} />
        </Routes>
      </main>
      <footer className="App-footer">
        <p>Nicla Sense ME 前端控制器 © 2023</p>
      </footer>
    </div>
  );
}

export default App;