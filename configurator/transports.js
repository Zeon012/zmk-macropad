// Connections to the pad. Each opener shows the browser's device chooser and
// resolves to { label, write(bytes), close() }. Incoming bytes go to onData;
// onClosed(message) fires if the link drops without close() being called.
// Classic browser script: window.MacropadTransports.
(function (root) {
  'use strict';

  // ZMK's USB ids, and the baud rate ZMK Studio uses for the serial transport.
  const USB_FILTER = { usbVendorId: 0x1d50, usbProductId: 0x615e };
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
    const port = await navigator.serial.requestPort({ filters: [USB_FILTER] });
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

  // The pad must already be paired with this computer, and unlocked so that
  // it is advertising and shows up in the chooser.
  async function openBluetooth(onData, onClosed) {
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
      const server = await device.gatt.connect();
      const service = await server.getPrimaryService(STUDIO_SERVICE);
      characteristic = await service.getCharacteristic(STUDIO_CHARACTERISTIC);
      characteristic.addEventListener('characteristicvaluechanged', event => {
        const value = event.target.value;
        onData(new Uint8Array(value.buffer, value.byteOffset, value.byteLength));
      });
      await characteristic.startNotifications();
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

  root.MacropadTransports = { openSerial, openBluetooth, PREFERRED_DEVICE_KEY, KNOWN_DEVICES_KEY };
})(typeof globalThis !== 'undefined' ? globalThis : this);
