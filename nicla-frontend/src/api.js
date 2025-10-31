const BASE_URL = 'http://localhost:8000/logs';

export async function scanDevices() {
  const resp = await fetch(`${BASE_URL}/scan`);
  return resp.json();
}

export async function connectDevice(address) {
  const resp = await fetch(`${BASE_URL}/connect/${encodeURIComponent(address)}`, {
    method: 'POST'
  });
  return resp.json();
}

export async function listRemoteFiles(address) {
  const resp = await fetch(`${BASE_URL}/remote-files/${encodeURIComponent(address)}`);
  return resp.json();
}

export async function pullSelected(address, files) {
  const resp = await fetch(`${BASE_URL}/pull-selected/${encodeURIComponent(address)}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ files })
  });
  return resp.json();
}

export async function listLocalFiles() {
  const resp = await fetch(`${BASE_URL}/files`);
  return resp.json();
}

export async function getLocalFileContent(filename) {
  const resp = await fetch(`${BASE_URL}/file/${encodeURIComponent(filename)}`);
  return resp.json();
}

export async function disconnectDevice(address) {
  const resp = await fetch(`${BASE_URL}/disconnect/${encodeURIComponent(address)}`, {
    method: 'POST'
  });
  return resp.json();
}
