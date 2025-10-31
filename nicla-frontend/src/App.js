import React, { useState } from 'react';
import Container from '@mui/material/Container';
import Paper from '@mui/material/Paper';
import Typography from '@mui/material/Typography';
import Box from '@mui/material/Box';

import ConnectPanel from './components/ConnectPanel';
import NavigationTabs from './components/NavigationTabs';
import PullFilesTab from './components/PullFilesTab';
import ViewLocalTab from './components/ViewLocalTab';

function App() {
  const [connectedDevice, setConnectedDevice] = useState(null);
  const [tabIndex, setTabIndex] = useState(0); // 0 = Pull, 1 = View Local

  const handleDeviceConnected = (address) => {
    setConnectedDevice(address);
    setTabIndex(0); // 默认切到 Pull Files
  };

  const handleDisconnect = () => {
    setConnectedDevice(null);
    setTabIndex(0);
  };

  if (!connectedDevice) {
    return (
      <Container maxWidth="md" sx={{ mt:4 }}>
        <Paper elevation={3} sx={{ p:3 }}>
          <Typography variant="h4" color="primary" gutterBottom>
            Nicla Sense ME Logger
          </Typography>
          <ConnectPanel onConnected={handleDeviceConnected} />
        </Paper>
      </Container>
    );
  }

  return (
    <Container maxWidth="md" sx={{ mt:4, mb:4 }}>
      <Paper elevation={3} sx={{ p:3 }}>
        <Box sx={{ display:'flex', justifyContent:'space-between', alignItems:'center' }}>
          <Typography variant="h5">Connected: {connectedDevice}</Typography>
          <Box>
            <button onClick={handleDisconnect}>Disconnect</button>
          </Box>
        </Box>

        <NavigationTabs tabIndex={tabIndex} setTabIndex={setTabIndex} />

        {tabIndex === 0 && (
          <PullFilesTab address={connectedDevice} />
        )}
        {tabIndex === 1 && (
          <ViewLocalTab />
        )}
      </Paper>
    </Container>
  );
}

export default App;
