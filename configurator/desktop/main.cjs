const { app, BrowserWindow, session } = require('electron');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');

const editorRoot = path.resolve(__dirname, '..');
let server;
let serialConfigured = false;

// USB ids of the devices the configurator knows (transports.js has the same list).
const USB_IDS = [[0x1d50, 0x615e], [0x1209, 0x0001]];

const contentTypes = {
  '.css': 'text/css; charset=utf-8',
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8'
};

function startEditorServer() {
  return new Promise((resolve, reject) => {
    server = http.createServer((request, response) => {
      const requestedPath = request.url === '/' ? '/index.html' : request.url;
      const filePath = path.resolve(editorRoot, `.${decodeURIComponent(requestedPath)}`);
      if (!filePath.startsWith(editorRoot) || !fs.existsSync(filePath)) {
        response.writeHead(404);
        response.end('Not found');
        return;
      }

      response.writeHead(200, {
        'Content-Type': contentTypes[path.extname(filePath)] || 'application/octet-stream',
        'Cache-Control': 'no-store'
      });
      fs.createReadStream(filePath).pipe(response);
    });

    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => resolve(server.address().port));
  });
}

function configureSerialPermissions() {
  if (serialConfigured) return;
  serialConfigured = true;
  const defaultSession = session.defaultSession;
  defaultSession.setPermissionCheckHandler((webContents, permission) => permission === 'serial');
  defaultSession.setPermissionRequestHandler((webContents, permission, callback) => {
    callback(permission === 'serial');
  });
  let lastPortId;
  defaultSession.on('select-serial-port', async (event, portList, webContents, callback) => {
    event.preventDefault();
    // ZMK devices mostly share one USB id (the KB60 has its own), so they are
    // told apart by serial number. The page stores which device the user
    // picked and the serial numbers it has learned; an empty id cancels when
    // no device is present.
    const candidates = portList.filter(({ vendorId, productId }) =>
      USB_IDS.some(([vid, pid]) => Number(vendorId) === vid && Number(productId) === pid));
    let wantedSerial = '';
    try {
      wantedSerial = await webContents.executeJavaScript(`(() => {
        try {
          const wanted = JSON.parse(localStorage.getItem('preferredDevice')) || '';
          const known = JSON.parse(localStorage.getItem('knownDevices')) || {};
          return known[wanted] || '';
        } catch { return ''; }
      })()`);
    } catch {}
    const chosen = wantedSerial &&
      candidates.find(({ serialNumber }) => (serialNumber || '').toUpperCase() === wantedSerial.toUpperCase());
    // Otherwise take the next one each time, so reconnecting steps through them.
    const next = chosen ||
      candidates[(candidates.findIndex(({ portId }) => portId === lastPortId) + 1) % (candidates.length || 1)];
    lastPortId = next ? next.portId : undefined;
    callback(next ? next.portId : '');
  });
  if (defaultSession.setDevicePermissionHandler) {
    defaultSession.setDevicePermissionHandler(({ deviceType }) => deviceType === 'serial');
  }
}

// Electron has no Bluetooth chooser of its own: pick the pad as soon as the scan
// sees it, and cancel if it has not turned up after a while.
const BLUETOOTH_SCAN_MS = 10000;
const BLUETOOTH_NAMES = ['Macropad', 'KB60'];

function configureBluetoothChooser(window) {
  let pendingCallback;
  let scanTimer;
  const finish = deviceId => {
    clearTimeout(scanTimer);
    const callback = pendingCallback;
    pendingCallback = undefined;
    if (callback) callback(deviceId);
  };
  window.webContents.on('select-bluetooth-device', async (event, devices, callback) => {
    event.preventDefault();
    if (!pendingCallback) scanTimer = setTimeout(() => finish(''), BLUETOOTH_SCAN_MS);
    pendingCallback = callback;
    // The page's selector narrows the search to one device name.
    let preferred = '';
    try {
      preferred = await window.webContents.executeJavaScript(
        `(() => { try { return JSON.parse(localStorage.getItem('preferredDevice')) || ''; } catch { return ''; } })()`);
    } catch {}
    const names = preferred ? [preferred] : BLUETOOTH_NAMES;
    const found = devices.find(({ deviceName }) => names.some(name => (deviceName || '').startsWith(name)));
    if (found) finish(found.deviceId);
  });
}

async function createWindow() {
  configureSerialPermissions();
  const port = await startEditorServer();
  const window = new BrowserWindow({
    width: 1440,
    height: 980,
    minWidth: 980,
    minHeight: 720,
    backgroundColor: '#1c1a23',
    autoHideMenuBar: true,
    icon: path.join(editorRoot, 'build', 'icon.ico'),
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false
    }
  });
  configureBluetoothChooser(window);
  await window.loadURL(`http://127.0.0.1:${port}/`);
}

app.whenReady().then(createWindow);

app.on('window-all-closed', () => {
  if (server) server.close();
  if (process.platform !== 'darwin') app.quit();
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) createWindow();
});