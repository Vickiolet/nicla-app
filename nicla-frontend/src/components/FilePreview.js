// src/components/FilePreview.js
import React, { useState, useEffect } from 'react';
import Box from '@mui/material/Box';
import Typography from '@mui/material/Typography';
import Button from '@mui/material/Button';

import { getLocalFileContent } from '../api';

export default function FilePreview({ filename }) {
  const [content, setContent] = useState('');
  const [loaded, setLoaded] = useState(false);

  useEffect(() => {
    if (!filename) {
      setContent('');
      setLoaded(false);
      return;
    }
    const load = async () => {
      try {
        const res = await getLocalFileContent(filename);
        if (res.content !== undefined) {
          setContent(res.content);
        } else {
          setContent('Failed to load content.');
        }
      } catch (err) {
        console.error('Error loading file content:', err);
        setContent('Error loading content.');
      }
      setLoaded(true);
    };
    load();
  }, [filename]);

  if (!filename) return null;

  return (
    <Box sx={{ mt:3 }}>
      <Typography variant="h6">Preview: {filename}</Typography>

      <Box component="pre"
           sx={{
             backgroundColor:'#f1f1f1',
             padding:2,
             borderRadius:1,
             maxHeight:400,
             overflow:'auto',
             fontFamily:'monospace',
             fontSize:'0.875rem',
             whiteSpace:'pre-wrap'
           }}>
        {loaded ? content : 'Loading...'}
      </Box>

      <Box sx={{ mt:2, display:'flex', justifyContent:'flex-end', gap:1 }}>
        <Button
          variant="outlined"
          size="small"
          onClick={() => {
            // reload preview content
            setLoaded(false);
            setContent('');
            // then load again
            (async () => {
              const res = await getLocalFileContent(filename);
              setContent(res.content !== undefined ? res.content : 'Failed to load content.');
              setLoaded(true);
            })();
          }}
        >
          Preview
        </Button>

        <Button
          variant="contained"
          size="small"
          onClick={async () => {
            try {
              const res = await getLocalFileContent(filename);
              if (res.content === undefined) {
                alert('Failed to fetch file content');
                return;
              }
              const blob = new Blob([res.content], { type: 'text/csv;charset=utf-8' });
              const url = window.URL.createObjectURL(blob);
              const link = document.createElement('a');
              link.href = url;
              link.download = filename;
              document.body.appendChild(link);
              link.click();
              document.body.removeChild(link);
              window.URL.revokeObjectURL(url);
            } catch (err) {
              console.error('Download error:', err);
              alert('Error downloading file: ' + err.message);
            }
          }}
        >
          Download
        </Button>
      </Box>
    </Box>
  );
}
