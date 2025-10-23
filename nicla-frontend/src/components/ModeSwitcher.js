import React, { useContext } from 'react';
import { AppContext } from '../context/AppContext';

function ModeSwitcher() {
  const {
    currentMode,
    switchMode,
    availableModes = ['正常模式', '调试模式', '低功耗模式'],
    addLog
  } = useContext(AppContext);

  const handleModeChange = (mode) => {
    if (mode !== currentMode) {
      addLog('信息', `切换到 ${mode}`);
      switchMode(mode);
    }
  };

  return (
    <div className="mode-switcher">
      <select 
        value={currentMode} 
        onChange={(e) => handleModeChange(e.target.value)}
      >
        {availableModes.map((mode) => (
          <option key={mode} value={mode}>{mode}</option>
        ))}
      </select>
    </div>
  );
}

export default ModeSwitcher;