const { app, BrowserWindow, session } = require('electron');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');

const editorRoot = path.resolve(__dirname, '..');
let server;
let serialConfigured = false;

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
  defaultSession.on('select-serial-port', (event, portList, webContents, callback) => {
    event.preventDefault();
    // ZMK's USB id; an empty id cancels the request when the pad is absent.
    const padPort = portList.find(({ vendorId, productId }) =>
      Number(vendorId) === 0x1d50 && Number(productId) === 0x615e);
    callback(padPort ? padPort.portId : '');
  });
  if (defaultSession.setDevicePermissionHandler) {
    defaultSession.setDevicePermissionHandler(({ deviceType }) => deviceType === 'serial');
  }
}

// Electron has no Bluetooth chooser of its own: pick the pad as soon as the scan
// sees it, and cancel if it has not turned up after a while. Once the page has
// learned the pad's address over USB, only that address is accepted.
const BLUETOOTH_SCAN_MS = 20000;
const BLUETOOTH_NAME_PREFIX = 'Macropad';
const BLUETOOTH_ADDRESS_KEY = 'padBluetoothAddress';

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
    let knownAddress = null;
    try {
      knownAddress = await window.webContents.executeJavaScript(
        `localStorage.getItem(${JSON.stringify(BLUETOOTH_ADDRESS_KEY)})`);
    } catch {}
    const pad = knownAddress
      ? devices.find(({ deviceId }) => (deviceId || '').toUpperCase() === knownAddress.toUpperCase())
      : devices.find(({ deviceName }) => (deviceName || '').startsWith(BLUETOOTH_NAME_PREFIX));
    if (pad) finish(pad.deviceId);
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