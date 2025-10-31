import React from 'react';
import Box from '@mui/material/Box';
import Tabs from '@mui/material/Tabs';
import Tab from '@mui/material/Tab';

export default function NavigationTabs({ tabIndex, setTabIndex }) {
  const handleChange = (_e, newValue) => {
    setTabIndex(newValue);
  };

  return (
    <Box sx={{ borderBottom: 1, borderColor: 'divider', mb:2 }}>
      <Tabs value={tabIndex} onChange={handleChange}>
        <Tab label="Pull Files" />
        <Tab label="View Local Files" />
      </Tabs>
    </Box>
  );
}
