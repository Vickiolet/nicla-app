import React, { useState } from 'react';
import Box from '@mui/material/Box';
import Button from '@mui/material/Button';
import FormControl from '@mui/material/FormControl';
import InputLabel from '@mui/material/InputLabel';
import Select from '@mui/material/Select';
import MenuItem from '@mui/material/MenuItem';
import Typography from '@mui/material/Typography';

import { scanDevices, connectDevice } from '../api';

export default function ConnectPanel({ onConnected }) {
  const [devices, setDevices] = useState([]);
  const [selected, setSelected] = useState('');
  const [scanning, setScanning] = useState(false);

  const handleScan = async () => {
    setScanning(true);
    const res = await scanDevices();
    setDevices(res); 
    setScanning(false);
  };

  const handleSelect = async (addr) => {
    setSelected(addr);
    const res = await connectDevice(addr);
    if (res.success) {
      onConnected(addr);
    } else {
      alert('Connect failed: ' + (res.message || ''));
    }
  };

  return (
    <Box sx={{ mt:2 }}>
      <Button variant="contained" color="primary" onClick={handleScan} disabled={scanning}>
        {scanning ? 'Scanning…' : 'Connect'}
      </Button>

      {devices.length > 0 && (
        <FormControl fullWidth sx={{ mt:2 }}>
          <InputLabel id="device-select-label">Select Device</InputLabel>
          <Select
            labelId="device-select-label"
            value={selected}
            label="Select Device"
            onChange={(e) => handleSelect(e.target.value)}
          >
            {devices.map((d) => (
              <MenuItem key={d.address} value={d.address}>
                {d.name || d.address}
              </MenuItem>
            ))}
          </Select>
        </FormControl>
      )}
    </Box>
  );
}
