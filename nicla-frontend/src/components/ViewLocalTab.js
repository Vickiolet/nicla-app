import React, { useState, useEffect } from 'react';
import Box from '@mui/material/Box';
import Typography from '@mui/material/Typography';
import Button from '@mui/material/Button';
import List from '@mui/material/List';
import ListItem from '@mui/material/ListItem';

import { listLocalFiles } from '../api';
import FilePreview from './FilePreview';

export default function ViewLocalTab() {
  const [files, setFiles] = useState([]);
  const [previewFile, setPreviewFile] = useState(null);

  useEffect(() => {
    const load = async () => {
      const res = await listLocalFiles();
      if (Array.isArray(res)) {
        setFiles(res);
      } else {
        alert('Failed to list local files: ' + JSON.stringify(res));
      }
    };
    load();
  }, []);

  return (
    <Box>
      <Typography variant="h6">Local Files</Typography>
      <List>
        {files.map(fn => (
          <ListItem key={fn} disablePadding sx={{ py:0.5, display:'flex', alignItems:'center', justifyContent:'space-between' }}>
            <span>{fn}</span>
            <Box sx={{ display:'flex', gap:1 }}>
              <Button variant="outlined" size="small" onClick={() => setPreviewFile(fn)}>
                Preview
              </Button>
              <Button variant="contained" size="small" component="a"
                href={`http://localhost:8000/logs/file/${encodeURIComponent(fn)}`}
                download
              >
                Download
              </Button>
            </Box>
          </ListItem>
        ))}
      </List>
      {previewFile && (
        <Box sx={{ mt:3 }}>
          <FilePreview filename={previewFile} />
        </Box>
      )}
    </Box>
  );
}
