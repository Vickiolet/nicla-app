import React, { useState, useEffect } from 'react';
import Box from '@mui/material/Box';
import Typography from '@mui/material/Typography';
import Button from '@mui/material/Button';
import List from '@mui/material/List';
import ListItem from '@mui/material/ListItem';
import Checkbox from '@mui/material/Checkbox';
import FormControlLabel from '@mui/material/FormControlLabel';

import { listRemoteFiles, pullSelected } from '../api';

export default function PullFilesTab({ address }) {
  const [files, setFiles] = useState([]);
  const [selected, setSelected] = useState(new Set());
  const [pulling, setPulling] = useState(false);

  useEffect(() => {
    const load = async () => {
      const res = await listRemoteFiles(address);
      if (res.success) {
        setFiles(res.files);
      } else {
        alert('Failed to get remote files: ' + (res.message || ''));
      }
    };
    load();
  }, [address]);

  const toggle = (fn) => {
    const s = new Set(selected);
    if (s.has(fn)) s.delete(fn);
    else s.add(fn);
    setSelected(s);
  };

  const handlePull = async () => {
    const arr = Array.from(selected);
    if (arr.length === 0) {
      alert('Please select at least one file');
      return;
    }
    setPulling(true);
    const res = await pullSelected(address, arr);
    if (res.success) {
      alert('Pull succeeded');
      // optional: you may clear selections or update
    } else {
      alert('Pull failed: ' + (res.message || ''));
    }
    setPulling(false);
  };

  return (
    <Box>
      <Typography variant="h6">Files on {address}</Typography>
      <List>
        {files.map(fn => (
          <ListItem key={fn} disablePadding sx={{ py:0.5 }}>
            <FormControlLabel
              control={
                <Checkbox
                  checked={selected.has(fn)}
                  onChange={() => toggle(fn)}
                />
              }
              label={fn}
            />
          </ListItem>
        ))}
      </List>
      <Box sx={{ mt:2 }}>
        <Button variant="contained" color="primary" onClick={handlePull} disabled={pulling}>
          {pulling ? 'Pulling…' : 'Pull Selected Files'}
        </Button>
      </Box>
    </Box>
  );
}
