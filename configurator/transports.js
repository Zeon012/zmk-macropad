// Connections to the pad. Each opener shows the browser's device chooser and
// resolves to { label, write(bytes), close() }. Incoming bytes go to onData;
// onClosed(message) fires if the link drops without close() being called.
// Classic browser script: window.MacropadTransports.
(function (root) {
  'use strict';

  // USB ids of the devices this app knows: ZMK's shared id (the macropad) and
  // the KB60's own (pid.codes test id). Also the baud rate ZMK Studio uses.
  const USB_FILTERS = [
    { usbVendorId: 0x1d50, usbProductId: 0x615e },
    { usbVendorId: 0x1209, usbProductId: 0x0001 }
  ];
  const BAUD_RATE = 12500;

  // ZMK Studio's GATT service and its single RPC characteristic.
  const STUDIO_SERVICE = '00000000-0196-6107-c967-c5cfb1c2482a';
  const STUDIO_CHARACTERISTIC = '00000001-0196-6107-c967-c5cfb1c2482a';
  // Fits the smallest Bluetooth packet, so no write needs a long-write the pad lacks.
  const BLUETOOTH_CHUNK = 20;
  // Bluetooth names of the devices this app knows. The selector narrows the
  // search to one of them; "Any device" looks for all.
  const BLUETOOTH_NAMES = ['Macropad', 'KB60'];
  // Remembered in the browser's storage: the device the user wants, and the
  // USB serial number of each device seen so far (the desktop app reads both
  // to open the right port).
  const PREFERRED_DEVICE_KEY = 'preferredDevice';
  const KNOWN_DEVICES_KEY = 'knownDevices';

  function bluetoothNames() {
    let preferred = '';
    try { preferred = JSON.parse(localStorage.getItem(PREFERRED_DEVICE_KEY)) || ''; } catch {}
    return preferred ? [preferred] : BLUETOOTH_NAMES;
  }

  async function openSerial(onData, onClosed) {
    if (!('serial' in navigator)) throw new Error('This browser cannot use USB serial. Use Chrome or Edge.');
    const port = await navigator.serial.requestPort({ filters: USB_FILTERS });
    await port.open({ baudRate: BAUD_RATE });
    const writer = port.writable.getWriter();
    const reader = port.readable.getReader();
    let closed = false;

    (async () => {
      let message = 'Device disconnected';
      try {
        for (;;) {
          const { value, done } = await reader.read();
          if (done) break;
          onData(value);
        }
      } catch (error) {
        message = error.message || message;
      }
      if (!closed) onClosed(message);
    })();

    return {
      label: 'USB',
      write: bytes => writer.write(bytes),
      async close() {
        closed = true;
        try { await reader.cancel(); } catch {}
        try { reader.releaseLock(); } catch {}
        try { writer.releaseLock(); } catch {}
        try { await port.close(); } catch {}
      }
    };
  }

  // Windows can leave a GATT connect waiting forever when the device is paired
  // but asleep or out of range, so the connection gives up after this long.
  const BLUETOOTH_CONNECT_MS = 10000;

  function withTimeout(promise, ms, message) {
    let timer;
    const expired = new Promise((_, reject) => { timer = setTimeout(() => reject(new Error(message)), ms); });
    return Promise.race([promise, expired]).finally(() => clearTimeout(timer));
  }

  // In the Android app the WebView has no Web Bluetooth, so the native
  // Bluetooth LE plugin (loaded only there) does the same job.
  function nativeBluetooth() {
    const native = root.Capacitor && root.Capacitor.isNativePlatform && root.Capacitor.isNativePlatform();
    return native && root.capacitorCommunityBluetoothLe ? root.capacitorCommunityBluetoothLe.BleClient : null;
  }

  async function openNativeBluetooth(ble, onData, onClosed) {
    await ble.initialize();
    if (!(await ble.isEnabled())) await ble.requestEnable();
    const names = bluetoothNames();
    let device;
    try {
      device = await ble.requestDevice(names.length === 1
        ? { namePrefix: names[0], optionalServices: [STUDIO_SERVICE] }
        : { optionalServices: [STUDIO_SERVICE] });
    } catch (error) {
      // Closing the chooser counts as no device, like in the browser.
      if (!/cancel/i.test(error.message || '')) throw error;
      const none = new Error(error.message);
      none.name = 'NotFoundError';
      throw none;
    }
    const id = device.deviceId;
    let closed = false;
    try {
      await ble.connect(id, () => { if (!closed) onClosed('Bluetooth disconnected'); }, { timeout: BLUETOOTH_CONNECT_MS });
      await ble.startNotifications(id, STUDIO_SERVICE, STUDIO_CHARACTERISTIC,
        view => onData(new Uint8Array(view.buffer, view.byteOffset, view.byteLength)));
    } catch (error) {
      closed = true;
      try { await ble.disconnect(id); } catch {}
      throw new Error(`Could not open the device over Bluetooth (${error.message}). Check that it is paired and unlocked.`);
    }

    let queue = Promise.resolve();
    return {
      label: 'Bluetooth',
      write(bytes) {
        const run = async () => {
          for (let offset = 0; offset < bytes.length; offset += BLUETOOTH_CHUNK) {
            const length = Math.min(BLUETOOTH_CHUNK, bytes.length - offset);
            await ble.write(id, STUDIO_SERVICE, STUDIO_CHARACTERISTIC, new DataView(bytes.buffer, bytes.byteOffset + offset, length));
          }
        };
        queue = queue.then(run, run);
        return queue;
      },
      async close() {
        closed = true;
        try { await ble.stopNotifications(id, STUDIO_SERVICE, STUDIO_CHARACTERISTIC); } catch {}
        try { await ble.disconnect(id); } catch {}
      }
    };
  }

  // The pad must already be paired with this computer, and unlocked so that
  // it is advertising and shows up in the chooser.
  async function openBluetooth(onData, onClosed) {
    const ble = nativeBluetooth();
    if (ble) return openNativeBluetooth(ble, onData, onClosed);
    if (!('bluetooth' in navigator)) throw new Error('This browser cannot use Bluetooth. Use Chrome or Edge.');
    const device = await navigator.bluetooth.requestDevice({
      filters: bluetoothNames().map(name => ({ namePrefix: name })),
      optionalServices: [STUDIO_SERVICE]
    });
    let closed = false;
    const onDisconnected = () => {
      if (!closed) onClosed('Bluetooth disconnected');
    };
    device.addEventListener('gattserverdisconnected', onDisconnected);

    let characteristic;
    try {
      characteristic = await withTimeout((async () => {
        const server = await device.gatt.connect();
        const service = await server.getPrimaryService(STUDIO_SERVICE);
        const found = await service.getCharacteristic(STUDIO_CHARACTERISTIC);
        found.addEventListener('characteristicvaluechanged', event => {
          const value = event.target.value;
          onData(new Uint8Array(value.buffer, value.byteOffset, value.byteLength));
        });
        await found.startNotifications();
        return found;
      })(), BLUETOOTH_CONNECT_MS, 'no answer within 10 seconds');
    } catch (error) {
      closed = true;
      device.removeEventListener('gattserverdisconnected', onDisconnected);
      try { device.gatt.disconnect(); } catch {}
      throw new Error(`Could not open the pad over Bluetooth (${error.message}). ` +
                      'Check that Windows is paired with it.');
    }

    let queue = Promise.resolve();
    return {
      label: 'Bluetooth',
      write(bytes) {
        const run = async () => {
          for (let offset = 0; offset < bytes.length; offset += BLUETOOTH_CHUNK) {
            await characteristic.writeValueWithResponse(bytes.subarray(offset, offset + BLUETOOTH_CHUNK));
          }
        };
        queue = queue.then(run, run);
        return queue;
      },
      async close() {
        closed = true;
        device.removeEventListener('gattserverdisconnected', onDisconnected);
        try { await characteristic.stopNotifications(); } catch {}
        try { device.gatt.disconnect(); } catch {}
      }
    };
  }

  root.MacropadTransports = {
    openSerial, openBluetooth, PREFERRED_DEVICE_KEY, KNOWN_DEVICES_KEY,
    // True inside the Android app, which connects over Bluetooth only.
    isNativeApp: Boolean(nativeBluetooth())
  };
})(typeof globalThis !== 'undefined' ? globalThis : this);
