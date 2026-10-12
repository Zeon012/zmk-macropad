// Key tables and binding helpers. A binding is { behavior, param1, param2 }
// where `behavior` is the ZMK display name; app.js maps names to device ids.
// Works as a classic browser script (window.MacropadKeys) and as a Node module.
(function (root) {
  'use strict';

  // ZMK encodes a key as (modifiers << 24) | (HID page << 16) | usage id.
  const PAGE_KEYBOARD = 0x07;
  const PAGE_CONSUMER = 0x0c;
  const MODIFIERS = [['Ctrl', 0x01], ['Shift', 0x02], ['Alt', 0x04], ['Win', 0x08]];

  const usage = (page, id) => ((page << 16) | id) >>> 0;
  const withMods = (code, mods) => (((mods & 0xff) << 24) | (code & 0x00ffffff)) >>> 0;
  const modsOf = code => code >>> 24;
  const baseOf = code => code & 0x00ffffff;

  const keyboardNames = new Map();
  const add = (id, name) => keyboardNames.set(id, name);
  for (let i = 0; i < 26; i++) add(0x04 + i, String.fromCharCode(65 + i));
  for (let i = 0; i < 9; i++) add(0x1e + i, String(i + 1));
  add(0x27, '0');
  [[0x28, 'Enter'], [0x29, 'Esc'], [0x2a, 'Backspace'], [0x2b, 'Tab'], [0x2c, 'Space'],
   [0x2d, '-'], [0x2e, '='], [0x2f, '['], [0x30, ']'], [0x31, '\\'], [0x33, ';'], [0x34, "'"],
   [0x35, '`'], [0x36, ','], [0x37, '.'], [0x38, '/'], [0x39, 'Caps Lock'],
   [0x46, 'Print Screen'], [0x47, 'Scroll Lock'], [0x48, 'Pause'], [0x49, 'Insert'],
   [0x4a, 'Home'], [0x4b, 'Page Up'], [0x4c, 'Delete'], [0x4d, 'End'], [0x4e, 'Page Down'],
   [0x4f, 'Right'], [0x50, 'Left'], [0x51, 'Down'], [0x52, 'Up'],
   [0xe0, 'Ctrl key'], [0xe1, 'Shift key'], [0xe2, 'Alt key'], [0xe3, 'Win key']
  ].forEach(([id, name]) => add(id, name));
  for (let i = 0; i < 12; i++) add(0x3a + i, `F${i + 1}`);
  for (let i = 0; i < 12; i++) add(0x68 + i, `F${i + 13}`);

  const consumerNames = new Map([
    [0xcd, 'Play / Pause'], [0xb5, 'Next track'], [0xb6, 'Previous track'], [0xb7, 'Stop'],
    [0xe9, 'Volume up'], [0xea, 'Volume down'], [0xe2, 'Mute'],
    [0x6f, 'Brightness up'], [0x70, 'Brightness down']
  ]);

  const keyboardId = name => [...keyboardNames].find(([, n]) => n === name)[0];
  const kp = (label, code) => ({ label, binding: { behavior: 'Key Press', param1: code >>> 0, param2: 0 } });
  const key = name => kp(name, usage(PAGE_KEYBOARD, keyboardId(name)));
  const combo = (label, mods, name) => kp(label, withMods(usage(PAGE_KEYBOARD, keyboardId(name)), mods));
  const plain = (label, behavior, param1 = 0, param2 = 0) => ({ label, binding: { behavior, param1, param2 } });

  // ZMK packs a scroll as two signed 16-bit speeds: horizontal in the high half.
  const SCROLL_SPEED = 80;
  const SCROLL_TAP_MS = 24;
  const scrollParam = (x, y) => ((((x & 0xffff) << 16) | (y & 0xffff)) >>> 0);
  const scroll = (label, x, y) => ({
    label, knobOnly: true,
    binding: { behavior: 'Mouse Scroll', param1: scrollParam(x, y), param2: 0, tapMs: SCROLL_TAP_MS }
  });
  const signed16 = value => (value << 16) >> 16;

  const letters = Array.from({ length: 26 }, (_, i) => String.fromCharCode(65 + i));
  const digits = ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0'];
  const range = (from, to) => Array.from({ length: to - from + 1 }, (_, i) => `F${from + i}`);
  const HYPER = 0x0f; // Ctrl + Shift + Alt + Win, the Microsoft 365 app shortcuts
  const WIN = 0x08;

  // Layer and macro entries are added at runtime from the device.
  const palette = {
    BASIC: [plain('None', 'None'), plain('Transparent', 'Transparent'),
            ...['Esc', 'Tab', 'Enter', 'Space', 'Backspace', 'Delete'].map(key),
            ...range(1, 24).map(key), ...letters.map(key), ...digits.map(key)],
    MEDIA: [...consumerNames].map(([id, name]) => kp(name, usage(PAGE_CONSUMER, id))),
    SPECIAL: [...['Ctrl key', 'Shift key', 'Alt key', 'Win key', 'Up', 'Down', 'Left', 'Right',
                  'Home', 'End', 'Page Up', 'Page Down', 'Insert', 'Print Screen', 'Caps Lock',
                  'Pause', 'Scroll Lock', '-', '=', '[', ']', '\\', ';', "'", '`', ',', '.', '/'].map(key)],
    APPS: [combo('LinkedIn', HYPER, 'L'), combo('OneNote', HYPER, 'N'), combo('Outlook', HYPER, 'O'),
           combo('PowerPoint', HYPER, 'P'), combo('Teams', HYPER, 'T'), combo('Word', HYPER, 'W'),
           combo('Excel', HYPER, 'X'), combo('Viva Engage', HYPER, 'Y'),
           combo('Win + X menu', WIN, 'X'), combo('Win + R Run', WIN, 'R'),
           combo('Win + E Explorer', WIN, 'E'), combo('Win + I Settings', WIN, 'I'),
           combo('Win + D Desktop', WIN, 'D'), combo('Win + L Lock', WIN, 'L')],
    // Scrolling is knob-only: the pad cannot check a scroll binding on a key.
    // 80 units a second for a 24 ms tap comes out as exactly one wheel notch.
    MOUSE: [scroll('Scroll up', 0, SCROLL_SPEED), scroll('Scroll down', 0, -SCROLL_SPEED),
            scroll('Scroll left', -SCROLL_SPEED, 0), scroll('Scroll right', SCROLL_SPEED, 0),
            plain('Left click', 'Mouse Key Press', 1), plain('Right click', 'Mouse Key Press', 2),
            plain('Middle click', 'Mouse Key Press', 4)],
    DEVICE: [plain('Bluetooth 1', 'Bluetooth', 3, 0), plain('Bluetooth 2', 'Bluetooth', 3, 1),
             plain('Bluetooth 3', 'Bluetooth', 3, 2), plain('Bluetooth clear', 'Bluetooth', 0, 0),
             plain('USB / Bluetooth', 'Output Selection', 0), plain('Studio unlock', 'Studio Unlock'),
             plain('Flashing mode', 'Bootloader'), plain('Restart', 'Reset')]
  };

  function keyName(code) {
    const page = (code >>> 16) & 0xff;
    const id = code & 0xffff;
    const names = page === PAGE_KEYBOARD ? keyboardNames : page === PAGE_CONSUMER ? consumerNames : null;
    return (names && names.get(id)) || `Key 0x${baseOf(code).toString(16)}`;
  }

  function modifierNames(mods) {
    // Right-hand modifiers share the low nibble's names.
    const merged = (mods | (mods >>> 4)) & 0x0f;
    return MODIFIERS.filter(([, bit]) => merged & bit).map(([name]) => name);
  }

  // Short text for a key cap. `layerName` resolves a layer id to its name.
  function bindingLabel(binding, layerName = id => `Layer ${id}`) {
    const { behavior, param1 = 0, param2 = 0 } = binding;
    switch (behavior) {
      case 'Key Press': return [...modifierNames(modsOf(param1)), keyName(param1)].join(' + ');
      case 'None': return 'None';
      case 'Transparent': return 'Transparent';
      case 'Momentary Layer': return `Hold: ${layerName(param1)}`;
      case 'Toggle Layer': return `Toggle: ${layerName(param1)}`;
      case 'To Layer': return `Go to: ${layerName(param1)}`;
      case 'Layer-Tap': return `${keyName(param2)} / hold: ${layerName(param1)}`;
      case 'Mod-Tap': return `${keyName(param2)} / hold: ${keyName(param1)}`;
      case 'Runtime Macro': return `Macro slot ${param1 + 1}`;
      case 'Mouse Scroll': {
        const x = signed16(param1 >>> 16);
        const y = signed16(param1 & 0xffff);
        if (y) return y > 0 ? 'Scroll up' : 'Scroll down';
        return x > 0 ? 'Scroll right' : x < 0 ? 'Scroll left' : 'Scroll';
      }
      case 'Mouse Key Press':
        return { 1: 'Left click', 2: 'Right click', 4: 'Middle click' }[param1] || 'Mouse button';
      case 'Bluetooth':
        if (param1 === 3) return `Bluetooth ${param2 + 1}`;
        return ['Bluetooth clear', 'Bluetooth next', 'Bluetooth previous', '', 'Bluetooth clear all',
                `Bluetooth disconnect ${param2 + 1}`][param1] || 'Bluetooth';
      case 'Output Selection': return ['USB / Bluetooth', 'USB output', 'Bluetooth output', 'No output'][param1] || 'Output';
      case 'Bootloader': return 'Flashing mode';
      case 'Reset': return 'Restart';
      case 'Studio Unlock': return 'Studio unlock';
      default: return behavior || 'Unknown';
    }
  }

  const ALIASES = { escape: 'esc', return: 'enter', del: 'delete', pgup: 'page up', pgdn: 'page down',
                    pageup: 'page up', pagedown: 'page down', prtsc: 'print screen', spacebar: 'space',
                    control: 'ctrl key', ctrl: 'ctrl key', shift: 'shift key', alt: 'alt key',
                    win: 'win key', gui: 'win key', cmd: 'win key' };

  // Key code for a name as typed by a person ("f13", "Page Up", "volume up"), or null.
  function codeForName(name) {
    const wanted = name.trim().toLowerCase();
    const resolved = ALIASES[wanted] || wanted;
    for (const [id, keyboardName] of keyboardNames) {
      if (keyboardName.toLowerCase() === resolved) return usage(PAGE_KEYBOARD, id);
    }
    for (const [id, consumerName] of consumerNames) {
      if (consumerName.toLowerCase() === resolved) return usage(PAGE_CONSUMER, id);
    }
    return null;
  }

  // Modifier bit for a name ("ctrl", "Win"), or 0.
  function modifierBit(name) {
    const wanted = name.trim().toLowerCase();
    const canonical = { control: 'ctrl', gui: 'win', cmd: 'win' }[wanted] || wanted;
    const found = MODIFIERS.find(([label]) => label.toLowerCase() === canonical);
    return found ? found[1] : 0;
  }

  const sameBinding = (a, b) =>
    a.behavior === b.behavior && (a.param1 >>> 0) === (b.param1 >>> 0) && (a.param2 >>> 0) === (b.param2 >>> 0);

  const api = { MODIFIERS, PAGE_KEYBOARD, palette, bindingLabel, keyName, modifierNames, codeForName, modifierBit,
                sameBinding, withMods, modsOf, baseOf, usage };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.MacropadKeys = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
