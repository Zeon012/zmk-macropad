const { StudioClient, StudioError, LOCK_STATE } = ZmkStudio;
const Keys = MacropadKeys;
const Macros = MacropadMacros;
const { Knob, KeyStream } = MacropadSubsystems;

const Transports = MacropadTransports;

// Key positions in keymap order (the matrix transform in macropad.overlay).
// Legends drawn like the printed keycaps.
const icon = paths => `<svg viewBox="0 0 24 24" aria-hidden="true">${paths}</svg>`;
const legends = {
  fast: icon('<path d="M13.5 2.5 5 13.5h5.5l-1 8 8.5-11h-5.5z"/>'),
  approve: icon('<circle cx="12" cy="12" r="8.5"/><path d="m8 12.3 2.8 2.8L16.2 9.5"/>'),
  reject: icon('<circle cx="12" cy="12" r="8.5"/><path d="m9 9 6 6m0-6-6 6"/>'),
  share: icon('<circle cx="6.5" cy="12" r="2.6"/><circle cx="17" cy="6" r="2.6"/><circle cx="17" cy="18" r="2.6"/>' +
              '<path d="m8.8 10.7 5.9-3.4M8.8 13.3l5.9 3.4"/>'),
  gameMode: 'SCROLL<span class="legend-box">GAME<br>MODE</span>'
};
const padControls = [
  ['Task 1', '1', 2, 1], ['Task 2', '2', 3, 1],
  ['Task 3', '3', 1, 2], ['Task 4', '4', 2, 2], ['Task 5', '5', 3, 2], ['Task 6', '6', 4, 2],
  ['Fast mode', legends.fast, 1, 3], ['Approve', legends.approve, 2, 3],
  ['Reject', legends.reject, 3, 3], ['Share', legends.share, 4, 3],
  ['Print Screen', 'PRINT<span class="legend-sub">SCR</span>', 2, 4, 'dark'], ['Break', 'BREAK', 3, 4, 'dark'], ['Game Mode', legends.gameMode, 4, 4, 'dark']
].map(([name, legend, column, row, cap = 'light']) => ({ name, legend, column, row, cap }));

const knobTurns = {
  ccw: { name: 'Knob left', request: 'set_layer_ccw_binding',
         arrow: icon('<path d="M5 8a8 8 0 1 1-1 7"/><path d="M5 3v5h5"/>') },
  cw: { name: 'Knob right', request: 'set_layer_cw_binding',
        arrow: icon('<path d="M19 8a8 8 0 1 0 1 7"/><path d="M19 3v5h-5"/>') }
};

// The macropad gets its own drawing; any other device is drawn from the
// layout it reports, and identified by its name.
const PAD_NAME = 'Macropad';
const UNLOCK_HINTS = {
  [PAD_NAME]: 'Hold Game Mode and press the Print Screen key',
  KB60: 'Hold Fn and press the \\ key'
};
// Friendlier names for the selector; anything else shows under its own name.
const DEVICE_LABELS = { [PAD_NAME]: 'Macropad', KB60: 'Keyboard (KB60)' };
const deviceLabel = name => DEVICE_LABELS[name] || name;
const GENERIC_UNIT_PX = 56;
const GENERIC_KEYS_PER_ROW = 14;

const SUBSYSTEM_QUERY_TIMEOUT_MS = 1500;
// Over Bluetooth the reply comes back in 20-byte pieces, and a phone is often
// still settling the link right after connecting, so it gets longer.
const SUBSYSTEM_QUERY_BLUETOOTH_TIMEOUT_MS = 5000;
const CLOSE_TIMEOUT_MS = 2000;
const STATUS_SUBSYSTEM = 'macropad__status';
const STATUS_POLL_MS = 5000;
const SET_BINDING_ERRORS = ['', 'That key position is not valid.', 'The device rejected that action.',
                            'The device rejected the settings for that action.'];
const categoryOrder = ['BASIC', 'MEDIA', 'MOUSE', 'LAYERS', 'SPECIAL', 'APPS', 'ANDROID', 'MACRO', 'DEVICE'];

const keymapPreview = document.querySelector('#keymapPreview');
const connectButton = document.querySelector('#connectButton');
const deviceSelect = document.querySelector('#deviceSelect');
const bluetoothButton = document.querySelector('#bluetoothButton');
const saveButton = document.querySelector('#saveButton');
const discardButton = document.querySelector('#discardButton');
const statusText = document.querySelector('#statusText');
const statusDot = document.querySelector('#statusDot');
const dirtyState = document.querySelector('#dirtyState');
const deviceName = document.querySelector('#deviceName');
const layerTabs = document.querySelector('#layerTabs');
const lockBanner = document.querySelector('#lockBanner');
const lockHint = document.querySelector('#lockHint');
const brand = document.querySelector('#brand');
const notice = document.querySelector('#notice');
const selectedName = document.querySelector('#selectedName');
const selectedSummary = document.querySelector('#selectedSummary');
const modToggles = document.querySelector('#modToggles');
const paletteRail = document.querySelector('#paletteRail');
const paletteKeys = document.querySelector('#paletteKeys');
const batteryStatus = document.querySelector('#batteryStatus');
const bluetoothStatus = document.querySelector('#bluetoothStatus');
const knobCaption = document.querySelector('#knobCaption');
const selectedKicker = document.querySelector('#selectedKicker');
const testerToggle = document.querySelector('#testerToggle');
const testerCount = document.querySelector('#testerCount');
const testerLogList = document.querySelector('#testerLog');
const macroDialog = document.querySelector('#macroDialog');
const macroDialogTitle = document.querySelector('#macroDialogTitle');
const macroName = document.querySelector('#macroName');
const macroInput = document.querySelector('#macroInput');
const macroInputLabel = document.querySelector('#macroInputLabel');
const macroInputHint = document.querySelector('#macroInputHint');
const macroPreset = document.querySelector('#macroPreset');
const macroAdmin = document.querySelector('#macroAdmin');
const macroAdminRow = document.querySelector('#macroAdminRow');
const macroError = document.querySelector('#macroError');
const macroDelete = document.querySelector('#macroDelete');
const macroSave = document.querySelector('#macroSave');

let transport;                 // the open USB or Bluetooth link, see transports.js
let connecting = false;        // a connect attempt is in progress
let controls = padControls;    // the keys being drawn, in keymap order
let connectedName = PAD_NAME;
const isPad = () => connectedName === PAD_NAME;
let client;
let locked = true;
let unsaved = false;
let layers = [];
let behaviorNames = new Map(); // device behavior id -> display name
let behaviorIds = new Map();   // display name -> device behavior id
let activeLayer = 0;
let selectedIndex = 0;
let activeCategory = 'BASIC';
let subsystems = new Map();    // custom subsystem identifier -> index on this device
let macros = [];               // { slot, name } for each macro stored on the device
let macroLimits = { maxNameLength: 24, keyPressBehaviorId: undefined };
let editingMacro = null;       // the macro open in the dialog, or null for a new one
let macroMode = 'text';
let statusTimer;
let statusLedState = 'off';    // 'usb', 'battery' or 'off', for the LED in the device drawing
let selectedKnob = null;       // 'cw' or 'ccw' while a knob turn is selected instead of a key
let knobBindings = new Map();  // layer id -> { cw, ccw }, each a binding plus tapMs
let testerOn = false;
let pressedKeys = new Set();   // key positions held down right now
let testedKeys = new Set();    // key positions seen since the tester started
let testerEvents = [];

const editable = () => Boolean(client) && !locked && layers.length > 0;
const layerLabel = (layer, index) => layer.name || `Layer ${index}`;
const layerNameById = id => {
  const index = layers.findIndex(layer => layer.id === id);
  return index < 0 ? `Layer ${id}` : layerLabel(layers[index], index);
};
const currentBinding = () => {
  const layer = layers[activeLayer];
  if (!layer) return undefined;
  return selectedKnob ? knobBindings.get(layer.id)?.[selectedKnob] : layer.bindings[selectedIndex];
};
const macroInSlot = slot => macros.find(macro => macro.slot === slot);

function labelFor(binding) {
  if (binding.behavior === 'Runtime Macro') {
    const macro = macroInSlot(binding.param1);
    return macro ? `Macro: ${macro.name}` : `Empty macro slot ${binding.param1 + 1}`;
  }
  return Keys.bindingLabel(binding, layerNameById);
}

function readStored(key, fallback) {
  try { return JSON.parse(localStorage.getItem(key)) ?? fallback; } catch { return fallback; }
}

function writeStored(key, value) {
  try { localStorage.setItem(key, JSON.stringify(value)); } catch {}
}

const preferredDevice = () => readStored(Transports.PREFERRED_DEVICE_KEY, '');

// Lists the two known devices plus any other the app has connected to.
function renderDeviceSelect() {
  const names = [...new Set([...Object.keys(DEVICE_LABELS), ...Object.keys(readStored(Transports.KNOWN_DEVICES_KEY, {}))])];
  deviceSelect.replaceChildren(new Option('Any device', ''), ...names.map(name => new Option(deviceLabel(name), name)));
  deviceSelect.value = names.includes(preferredDevice()) ? preferredDevice() : '';
  deviceSelect.disabled = Boolean(transport);
}

// Each device's USB serial number is what lets the desktop app open the right
// port next time without trying them in turn.
function rememberDevice(name, serialBytes) {
  if (!serialBytes?.length) return;
  const serial = [...serialBytes].map(byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase();
  writeStored(Transports.KNOWN_DEVICES_KEY, { ...readStored(Transports.KNOWN_DEVICES_KEY, {}), [name]: serial });
}

function showNotice(text = '', isError = false) {
  notice.textContent = text;
  notice.classList.toggle('error', isError);
}

function describeError(error) {
  if (error instanceof StudioError && error.code === 'UNLOCK_REQUIRED') return 'The device is locked.';
  return error.message || String(error);
}

function setUnsaved(value) {
  unsaved = value;
  if (!client) dirtyState.textContent = 'Not connected';
  else if (locked) dirtyState.textContent = 'Locked';
  else dirtyState.textContent = value ? 'Unsaved changes: the device forgets them on restart' : 'All changes saved';
  dirtyState.style.color = value && editable() ? '#f4d878' : '#d6e9d9';
  saveButton.disabled = !editable() || !value;
  discardButton.disabled = !editable() || !value;
}

function renderLayerTabs() {
  layerTabs.replaceChildren(...layers.map((layer, index) => {
    const tab = document.createElement('button');
    tab.className = `via-tab${index === activeLayer ? ' active' : ''}`;
    tab.textContent = layerLabel(layer, index);
    tab.addEventListener('click', () => {
      activeLayer = index;
      render();
    });
    return tab;
  }));
}

function keyClasses(index) {
  return `${!selectedKnob && index === selectedIndex ? ' selected' : ''}` +
    `${pressedKeys.has(index) ? ' key-test-hit' : ''}${testerOn && testedKeys.has(index) ? ' tested' : ''}`;
}

// Cap colour for a drawn keyboard: letters and symbols, navigation, the rest.
function genericCap(binding) {
  if (!binding || binding.behavior !== 'Key Press') return 'mod';
  const name = Keys.keyName(binding.param1);
  if (['Space', 'Up', 'Down', 'Left', 'Right'].includes(name)) return 'accent';
  return name.length === 1 ? 'alpha' : 'mod';
}

// Any device other than the macropad: keys placed where its layout says.
function renderGenericBoard() {
  knobCaption.hidden = true;
  keymapPreview.className = `keymap-preview generic${editable() ? '' : ' disabled'}`;
  if (!controls.length) {
    const empty = document.createElement('p');
    empty.className = 'board-empty';
    empty.textContent = 'Unlock the keyboard to load its layout.';
    keymapPreview.style.cssText = '';
    keymapPreview.replaceChildren(empty);
    return;
  }
  const right = Math.max(...controls.map(key => key.x + key.w));
  const bottom = Math.max(...controls.map(key => key.y + key.h));
  const available = keymapPreview.parentElement.clientWidth - 80;
  const unit = Math.max(24, Math.min(GENERIC_UNIT_PX, Math.floor(available / (right / 100))));
  keymapPreview.style.cssText = `width: ${(right / 100) * unit + 20}px; height: ${(bottom / 100) * unit + 20}px`;

  const bindings = layers[activeLayer]?.bindings || [];
  keymapPreview.replaceChildren(...controls.map((control, index) => {
    const button = document.createElement('button');
    button.className = `map-key generic-key cap-${genericCap(bindings[index])}${keyClasses(index)}`;
    button.style.cssText = `left: ${(control.x / 100) * unit + 10}px; top: ${(control.y / 100) * unit + 10}px; ` +
      `width: ${(control.w / 100) * unit - 6}px; height: ${(control.h / 100) * unit - 6}px`;
    button.title = control.name;
    const label = document.createElement('span');
    label.className = 'generic-label';
    label.textContent = bindings[index] ? labelFor(bindings[index]) : '';
    button.append(label);
    button.addEventListener('click', () => {
      selectedIndex = index;
      selectedKnob = null;
      render();
    });
    return button;
  }));
}

function renderKeymap() {
  if (!isPad()) {
    renderGenericBoard();
    return;
  }
  keymapPreview.className = `keymap-preview${editable() ? '' : ' disabled'}`;
  keymapPreview.style.cssText = '';
  const knob = document.createElement('div');
  knob.className = 'knob-cluster';
  knob.style.cssText = 'grid-column: 1; grid-row: 1';
  const knobCap = document.createElement('div');
  knobCap.className = 'map-knob';
  const layerKnob = knobBindings.get(layers[activeLayer]?.id);
  const turnButtons = Object.entries(knobTurns).map(([direction, turn]) => {
    const button = document.createElement('button');
    button.className = `knob-turn knob-${direction}${selectedKnob === direction ? ' selected' : ''}`;
    button.innerHTML = turn.arrow;
    button.title = layerKnob ? `${turn.name}: ${labelFor(layerKnob[direction])}` : `${turn.name}: not available in this firmware`;
    button.disabled = !layerKnob;
    button.addEventListener('click', () => {
      selectedKnob = direction;
      render();
    });
    return button;
  });
  knob.append(turnButtons[0], knobCap, turnButtons[1]);
  knobCaption.hidden = !layerKnob;
  if (layerKnob) {
    knobCaption.textContent = `Knob left: ${labelFor(layerKnob.ccw)}  ·  Knob right: ${labelFor(layerKnob.cw)}`;
  }
  const screws = ['top-left', 'top-right', 'bottom-left', 'bottom-right'].map(corner => {
    const screw = document.createElement('span');
    screw.className = `plate-screw ${corner}`;
    return screw;
  });

  const bindings = layers[activeLayer]?.bindings || [];
  const keys = controls.map((control, index) => {
    const button = document.createElement('button');
    button.className = `map-key cap-${control.cap}${keyClasses(index)}`;
    button.style.cssText = `grid-column: ${control.column}; grid-row: ${control.row}`;
    button.title = control.name;
    const symbol = document.createElement('span');
    symbol.className = 'key-symbol';
    symbol.innerHTML = control.legend;
    const label = document.createElement('small');
    label.textContent = bindings[index] ? labelFor(bindings[index]) : '';
    button.append(symbol, label);
    button.addEventListener('click', () => {
      selectedIndex = index;
      selectedKnob = null;
      render();
    });
    return button;
  });
  // The status LED sits in the empty top-right position and mirrors the real one.
  const statusLed = document.createElement('span');
  statusLed.className = `status-led ${statusLedState}`;
  statusLed.style.cssText = 'grid-column: 4; grid-row: 1';
  statusLed.title = { usb: 'Status LED: green on USB power', battery: 'Status LED: red on battery',
                      off: 'Status LED' }[statusLedState];
  keymapPreview.replaceChildren(...screws, knob, statusLed, ...keys);
}

function renderInspector() {
  const binding = currentBinding();
  selectedKicker.textContent = selectedKnob ? 'Selected knob turn' : 'Selected key';
  selectedName.textContent = selectedKnob ? knobTurns[selectedKnob].name : controls[selectedIndex]?.name || 'No key';
  selectedSummary.textContent = binding ? labelFor(binding) : 'Not loaded';

  const isKeyPress = Boolean(binding) && binding.behavior === 'Key Press';
  const mods = isKeyPress ? Keys.modsOf(binding.param1) : 0;
  modToggles.replaceChildren(...Keys.MODIFIERS.map(([name, bit]) => {
    const toggle = document.createElement('button');
    toggle.className = `mod-toggle${mods & bit ? ' active' : ''}`;
    toggle.textContent = name;
    toggle.disabled = !editable() || !isKeyPress;
    toggle.addEventListener('click', () => {
      assign({ ...binding, param1: Keys.withMods(binding.param1, mods ^ bit) });
    });
    return toggle;
  }));
}

function paletteEntries(category) {
  if (category === 'MACRO') {
    return macros.map(macro => ({ label: macro.name, binding: { behavior: 'Runtime Macro', param1: macro.slot, param2: 0 } }));
  }
  if (category !== 'LAYERS') return Keys.palette[category];
  return layers.flatMap((layer, index) => {
    const name = layerLabel(layer, index);
    return [['Hold', 'Momentary Layer'], ['Toggle', 'Toggle Layer'], ['Go to', 'To Layer']].map(
      ([verb, behavior]) => ({ label: `${verb}: ${name}`, binding: { behavior, param1: layer.id, param2: 0 } }));
  });
}

function renderPalette() {
  paletteRail.replaceChildren(...categoryOrder.map(category => {
    const tab = document.createElement('button');
    tab.className = `palette-category${category === activeCategory ? ' active' : ''}`;
    tab.textContent = category;
    tab.addEventListener('click', () => {
      activeCategory = category;
      renderPalette();
    });
    return tab;
  }));

  const entries = paletteEntries(activeCategory);
  if (activeCategory === 'MACRO') {
    renderMacroPalette(entries);
    return;
  }
  if (!entries.length) {
    const empty = document.createElement('span');
    empty.className = 'palette-empty';
    empty.textContent = 'Connect and unlock the device to see its layers.';
    paletteKeys.replaceChildren(empty);
    return;
  }
  const binding = currentBinding();
  paletteKeys.replaceChildren(...entries.map(entry => {
    const button = document.createElement('button');
    const available = behaviorIds.has(entry.binding.behavior);
    // A key press with modifiers still highlights its base key.
    const candidate = binding && binding.behavior === 'Key Press' && entry.binding.behavior === 'Key Press' &&
                      Keys.modsOf(entry.binding.param1) === 0
      ? { ...binding, param1: Keys.baseOf(binding.param1) } : binding;
    button.className = `palette-key${candidate && Keys.sameBinding(candidate, entry.binding) ? ' active' : ''}`;
    button.textContent = entry.label;
    const wrongControl = Boolean(entry.knobOnly) && !selectedKnob;
    button.disabled = !editable() || !available || wrongControl;
    if (client && !locked && !available) button.title = 'Not available in this firmware';
    else if (wrongControl) button.title = 'Scrolling can only go on the knob: select a knob arrow first';
    button.addEventListener('click', () => assign(entry.binding));
    return button;
  }));
}

// Each macro gets a button that assigns it to the selected key and one that
// opens it for editing.
function renderMacroPalette(entries) {
  const supported = subsystems.has(Macros.SUBSYSTEM) && behaviorIds.has('Runtime Macro');
  if (client && !locked && !supported) {
    const missing = document.createElement('span');
    missing.className = 'palette-empty';
    missing.textContent = 'This firmware has no runtime macro support.';
    paletteKeys.replaceChildren(missing);
    return;
  }
  const binding = currentBinding();
  const buttons = entries.flatMap((entry, index) => {
    const assignButton = document.createElement('button');
    assignButton.className = `palette-key${binding && Keys.sameBinding(binding, entry.binding) ? ' active' : ''}`;
    assignButton.textContent = entry.label;
    assignButton.title = 'Assign to the selected key';
    assignButton.disabled = !editable();
    assignButton.addEventListener('click', () => assign(entry.binding));
    const editButton = document.createElement('button');
    editButton.className = 'palette-key palette-edit';
    editButton.textContent = `Edit ${entry.label}`;
    editButton.disabled = !editable();
    editButton.addEventListener('click', () => openMacroDialog(macros[index]));
    return [assignButton, editButton];
  });
  const newButton = document.createElement('button');
  newButton.className = 'palette-key palette-new';
  newButton.textContent = '+ New macro';
  newButton.disabled = !editable();
  newButton.addEventListener('click', () => openMacroDialog(null));
  paletteKeys.replaceChildren(...buttons, newButton);
}

function renderTester() {
  const supported = subsystems.has(KeyStream.SUBSYSTEM);
  testerToggle.disabled = !editable() || !supported;
  testerToggle.textContent = testerOn ? 'Stop key tester' : 'Start key tester';
  testerToggle.title = client && !locked && !supported ? 'Not available in this firmware' : '';
  testerCount.textContent = testerOn ? `${testedKeys.size} of ${controls.length} seen` : '';
  testerLogList.replaceChildren(...testerEvents.map(text => {
    const entry = document.createElement('li');
    entry.textContent = text;
    return entry;
  }));
}

function render() {
  lockBanner.hidden = !client || !locked;
  lockHint.textContent = `${UNLOCK_HINTS[connectedName] || 'Press its Studio unlock key'} to unlock it for editing.`;
  brand.textContent = (client ? connectedName : 'Macro pad').toUpperCase();
  bluetoothButton.hidden = Boolean(transport);
  // The Android app has no USB: this button only appears as Disconnect.
  if (Transports.isNativeApp) connectButton.hidden = !transport;
  renderDeviceSelect();
  renderTester();
  renderLayerTabs();
  renderKeymap();
  renderInspector();
  renderPalette();
  setUnsaved(unsaved);
}

function countBindings(behavior) {
  return layers.reduce((total, layer) => total + layer.bindings.filter(b => b.behavior === behavior).length, 0);
}

async function assign(binding) {
  if (!editable()) return;
  if (selectedKnob) {
    await assignKnob(binding);
    return;
  }
  const layer = layers[activeLayer];
  const previous = layer.bindings[selectedIndex];
  const behaviorId = behaviorIds.get(binding.behavior);
  if (behaviorId === undefined) {
    showNotice(`This firmware has no "${binding.behavior}" action.`, true);
    return;
  }
  if (previous.behavior === 'Studio Unlock' && binding.behavior !== 'Studio Unlock' &&
      countBindings('Studio Unlock') === 1 &&
      !confirm('This is the only Studio unlock key. Without one you cannot unlock the device for editing ' +
               'again until you reflash the settings reset file. Replace it anyway?')) {
    return;
  }
  try {
    const result = await client.call('keymap', 'set_layer_binding', {
      layer_id: layer.id,
      key_position: selectedIndex,
      binding: { behavior_id: behaviorId, param1: binding.param1 >>> 0, param2: binding.param2 >>> 0 }
    });
    if (result !== 0) {
      showNotice(SET_BINDING_ERRORS[result] || 'The device rejected the change.', true);
      return;
    }
    layer.bindings[selectedIndex] = { behavior: binding.behavior, param1: binding.param1 >>> 0, param2: binding.param2 >>> 0 };
    showNotice();
    unsaved = true;
  } catch (error) {
    showNotice(describeError(error), true);
  }
  render();
}

// Calls a custom Studio subsystem with a raw payload and returns the reply payload.
async function customCall(identifier, payload) {
  const index = subsystems.get(identifier);
  if (index === undefined) throw new Error(`This firmware has no ${identifier} support.`);
  const reply = await client.call('custom', 'call', { subsystem_index: index, payload });
  return reply.payload || new Uint8Array();
}

// Official ZMK has no custom subsystems and never answers the question, so
// this waits only briefly; no answer means none of the add-on features.
async function loadSubsystems() {
  subsystems = new Map();
  try {
    const timeoutMs = transport?.label === 'Bluetooth' ? SUBSYSTEM_QUERY_BLUETOOTH_TIMEOUT_MS : SUBSYSTEM_QUERY_TIMEOUT_MS;
    const reply = await client.call('custom', 'list_custom_subsystems', {}, timeoutMs);
    subsystems = new Map(reply.subsystems.map(subsystem => [subsystem.identifier ?? '', subsystem.index ?? 0]));
  } catch {}
}

// Reply is [version, battery percent, flags, Bluetooth profile]. Flags: bit 0 USB
// power, bit 1 Bluetooth host connected, bit 2 pairing mode, bit 3 Bluetooth available.
async function refreshStatus() {
  if (!client || !subsystems.has(STATUS_SUBSYSTEM)) {
    batteryStatus.hidden = true;
    bluetoothStatus.hidden = true;
    return;
  }
  try {
    const [version, percent, flags, profile = 0] = await customCall(STATUS_SUBSYSTEM, Uint8Array.of(1));
    if (version !== 1) return;
    batteryStatus.textContent = `Battery ${percent}%${flags & 1 ? ' · USB power' : ''}`;
    batteryStatus.hidden = false;
    const state = flags & 2 ? 'connected' : flags & 4 ? 'pairing mode' : 'paired, host not connected';
    bluetoothStatus.textContent = `Bluetooth ${profile + 1}: ${state}`;
    const ledState = flags & 1 ? 'usb' : 'battery';
    if (ledState !== statusLedState) {
      statusLedState = ledState;
      renderKeymap();
    }
    bluetoothStatus.hidden = !(flags & 8);
  } catch {
    batteryStatus.hidden = true;
    bluetoothStatus.hidden = true;
  }
}

// Calls a firmware module that speaks protobuf: `spec` holds its subsystem
// name and Request/Response schemas.
async function subsystemCall(spec, type, value = {}) {
  const request = ZmkStudio.encode(spec.Request, { [type]: value });
  const response = ZmkStudio.decode(spec.Response, await customCall(spec.SUBSYSTEM, request));
  if (response.error) throw new Error(response.error.message || 'The device rejected the request.');
  return response[type] ?? response.ok ?? {};
}

function knobBinding(stored) {
  const id = stored?.behavior_id ?? 0;
  const tapMs = stored?.tap_ms || Knob.DEFAULT_TAP_MS;
  if (!id) return { behavior: 'Not set', param1: 0, param2: 0, tapMs };
  return { behavior: behaviorNames.get(id) || `Behavior ${id}`, param1: stored.param1 ?? 0, param2: stored.param2 ?? 0, tapMs };
}

// The knob module keys its bindings by layer id and saves each change at once.
async function loadKnob() {
  knobBindings = new Map();
  if (!subsystems.has(Knob.SUBSYSTEM)) return;
  const reply = await subsystemCall(Knob, 'get_all_layer_bindings', { sensor_index: 0 });
  for (const entry of reply.bindings || []) {
    knobBindings.set(entry.layer ?? 0, { cw: knobBinding(entry.cw_binding), ccw: knobBinding(entry.ccw_binding) });
  }
}

async function assignKnob(binding) {
  const layer = layers[activeLayer];
  const direction = selectedKnob;
  const behaviorId = behaviorIds.get(binding.behavior);
  if (behaviorId === undefined) {
    showNotice(`This firmware has no "${binding.behavior}" action.`, true);
    return;
  }
  const turns = knobBindings.get(layer.id);
  // Scroll bindings bring their own tap length; everything else uses the default.
  const tapMs = binding.tapMs || Knob.DEFAULT_TAP_MS;
  try {
    const reply = await subsystemCall(Knob, knobTurns[direction].request, {
      sensor_index: 0,
      layer: layer.id,
      binding: { behavior_id: behaviorId, param1: binding.param1 >>> 0, param2: binding.param2 >>> 0, tap_ms: tapMs }
    });
    if (!reply.success) throw new Error('The device did not accept the knob change.');
    knobBindings.set(layer.id, { ...turns, [direction]: { behavior: binding.behavior, param1: binding.param1 >>> 0, param2: binding.param2 >>> 0, tapMs } });
    showNotice(`${knobTurns[direction].name} saved to the device.`);
  } catch (error) {
    showNotice(describeError(error), true);
  }
  render();
}

function resetTester() {
  testerOn = false;
  pressedKeys = new Set();
}

async function setTester(on) {
  try {
    await subsystemCall(KeyStream, on ? 'enable_stream' : 'disable_stream');
    resetTester();
    testerOn = on;
    if (on) {
      testedKeys = new Set();
      testerEvents = [];
    }
  } catch (error) {
    showNotice(describeError(error), true);
  }
  render();
}

function handleKeyStream(payload) {
  const event = ZmkStudio.decode(KeyStream.Notification, payload).key_event;
  if (!event) return;
  const position = event.position ?? 0;
  if (event.pressed) {
    pressedKeys.add(position);
    testedKeys.add(position);
    testerEvents = [controls[position]?.name || `Key ${position}`, ...testerEvents].slice(0, 12);
  } else {
    pressedKeys.delete(position);
  }
  renderKeymap();
  renderTester();
}

async function macroCall(type, value = {}) {
  const request = ZmkStudio.encode(Macros.Request, { [type]: value });
  const response = ZmkStudio.decode(Macros.Response, await customCall(Macros.SUBSYSTEM, request));
  if (response.error) throw new Error(response.error.message || 'The device rejected the macro change.');
  return response[type] ?? response.status ?? {};
}

// The macro list and limits can be read while the device is locked.
async function loadMacros() {
  if (!subsystems.has(Macros.SUBSYSTEM)) {
    macros = [];
    return;
  }
  const list = await macroCall('list_macros');
  macros = (list.macros || []).map(macro => ({ slot: macro.slot ?? 0, name: macro.name ?? '' }));
  const { settings = {} } = await macroCall('get_macro_global_settings');
  macroLimits = {
    maxNameLength: list.max_name_length || 24,
    keyPressBehaviorId: settings.key_press_behavior_id
  };
}

function setMacroMode(mode) {
  macroMode = mode;
  document.querySelectorAll('[data-macro-mode]').forEach(tab => tab.classList.toggle('active', tab.dataset.macroMode === mode));
  const text = {
    text: ['Text to type', 'Example: hello@example.com',
           `Up to ${Macros.MAX_TEXT_LENGTH} characters. A new line presses Enter.`],
    combo: ['Key combos', 'Example: Ctrl+Shift+S, 150ms, Enter',
            'Separate combos with commas. Add a pause like 150ms between them when a menu needs time to open.'],
    program: ['Program, command or path', 'Example: wt',
              'Opened through Win+R. For any other program, paste its full path ' +
              '(Shift+right-click the .exe, Copy as path). Works on any Windows PC, no app needed.']
  }[mode];
  [macroInputLabel.textContent, macroInput.placeholder, macroInputHint.textContent] = text;
  macroInput.rows = mode === 'program' ? 1 : 3;
  let presets;
  if (mode === 'combo') {
    presets = Macros.presets.map(preset => new Option(preset.name, preset.combo));
  } else {
    const groups = new Map();
    for (const preset of Macros.programPresets) {
      if (!groups.has(preset.group)) {
        const optgroup = document.createElement('optgroup');
        optgroup.label = preset.group;
        groups.set(preset.group, optgroup);
      }
      groups.get(preset.group).append(new Option(`${preset.name} (${preset.command})`, preset.command));
    }
    presets = [...groups.values()];
  }
  macroPreset.replaceChildren(new Option(mode === 'combo' ? 'Insert a quick link...' : 'Pick a program...', ''), ...presets);
  macroPreset.hidden = mode === 'text';
  macroAdminRow.hidden = mode !== 'program';
}

async function openMacroDialog(macro) {
  editingMacro = macro;
  macroError.textContent = '';
  macroDialogTitle.textContent = macro ? `Edit macro: ${macro.name}` : 'New macro';
  macroName.value = macro ? macro.name : '';
  macroName.disabled = Boolean(macro);
  macroName.maxLength = macroLimits.maxNameLength;
  macroDelete.hidden = !macro;
  macroInput.value = '';
  macroAdmin.checked = false;
  setMacroMode('text');
  if (macro) {
    try {
      const { macro: detail = {} } = await macroCall('get_macro', { slot: macro.slot });
      const editableSteps = Macros.stepsToEditable(detail.steps || [], macroLimits.keyPressBehaviorId);
      if (editableSteps.mode === 'custom') {
        macroError.textContent = 'This macro has steps this editor cannot show. Saving replaces them.';
      } else {
        setMacroMode(editableSteps.mode);
        macroInput.value = editableSteps.value;
        macroAdmin.checked = Boolean(editableSteps.admin);
      }
    } catch (error) {
      showNotice(describeError(error), true);
      return;
    }
  }
  macroDialog.showModal();
  (macro ? macroInput : macroName).focus();
}

async function saveMacro() {
  const name = macroName.value.trim();
  if (!editingMacro) {
    if (!name) return void (macroError.textContent = 'Give the macro a name.');
    if (macros.some(macro => macro.name === name)) return void (macroError.textContent = 'A macro with that name already exists.');
  }
  const keyPressId = macroLimits.keyPressBehaviorId ?? behaviorIds.get('Key Press');
  const converted = macroMode === 'combo' ? Macros.comboToSteps(macroInput.value, keyPressId)
    : macroMode === 'program' ? Macros.programToSteps(macroInput.value, macroAdmin.checked, keyPressId)
    : Macros.textToSteps(macroInput.value);
  if (converted.error) return void (macroError.textContent = converted.error);

  macroSave.disabled = true;
  try {
    let slot;
    if (editingMacro) {
      slot = editingMacro.slot;
      await macroCall('set_macro_step_count', { slot, step_count: 0, persist: false });
    } else {
      await macroCall('create_macro', { name, persist: false });
      await loadMacros();
      const created = macros.find(macro => macro.name === name);
      if (!created) throw new Error('The device did not report the new macro.');
      slot = created.slot;
    }
    for (const step of converted.steps) await macroCall('append_macro_step', { slot, step, persist: false });
    await macroCall('save_macros');
    await loadMacros();
    macroDialog.close();
    showNotice(`Macro "${editingMacro ? editingMacro.name : name}" saved to the device.`);
  } catch (error) {
    // Drop the half-written macro and go back to what is stored.
    try { await macroCall('discard_macros'); } catch {}
    try { await loadMacros(); } catch {}
    macroError.textContent = describeError(error);
  }
  macroSave.disabled = false;
  render();
}

async function deleteMacro() {
  const macro = editingMacro;
  const uses = layers.reduce((total, layer) => total +
    layer.bindings.filter(b => b.behavior === 'Runtime Macro' && b.param1 === macro.slot).length, 0);
  const warning = uses ? ` ${uses} key${uses === 1 ? ' uses' : 's use'} it and will do nothing afterwards.` : '';
  if (!confirm(`Delete the macro "${macro.name}"?${warning}`)) return;
  try {
    await macroCall('delete_macro', { name: macro.name });
    await loadMacros();
    macroDialog.close();
    showNotice(`Macro "${macro.name}" deleted.`);
  } catch (error) {
    macroError.textContent = describeError(error);
  }
  render();
}

// Builds the key list for a device other than the macropad from the layout
// it reports. A layout with no key positions falls back to a plain grid.
async function loadLayout() {
  const keyCount = layers[0]?.bindings.length || 0;
  let keys = [];
  try {
    const reply = await client.call('keymap', 'get_physical_layouts');
    keys = reply.layouts[reply.active_layout_index ?? 0]?.keys || [];
  } catch {}
  if (keys.length !== keyCount) {
    keys = Array.from({ length: keyCount }, (_, index) => ({
      x: (index % GENERIC_KEYS_PER_ROW) * 100, y: Math.floor(index / GENERIC_KEYS_PER_ROW) * 100
    }));
  }
  controls = keys.map((key, index) => ({
    name: `Key ${index + 1}`, x: key.x ?? 0, y: key.y ?? 0, w: key.width ?? 100, h: key.height ?? 100
  }));
  if (selectedIndex >= controls.length) selectedIndex = 0;
}

async function loadKeymap() {
  const keymap = await client.call('keymap', 'get_keymap');
  layers = keymap.layers.map(layer => ({
    id: layer.id ?? 0,
    name: layer.name ?? '',
    bindings: layer.bindings.map(binding => ({
      behavior: behaviorNames.get(binding.behavior_id ?? 0) || `Behavior ${binding.behavior_id ?? 0}`,
      param1: binding.param1 ?? 0,
      param2: binding.param2 ?? 0
    }))
  }));
  if (activeLayer >= layers.length) activeLayer = 0;
  unsaved = await client.call('keymap', 'check_unsaved_changes');
}

// Needs an unlocked pad: behavior details and the keymap are both secured.
async function loadDevice() {
  showNotice('Reading the device...');
  const { behaviors } = await client.call('behaviors', 'list_all_behaviors');
  behaviorNames = new Map();
  behaviorIds = new Map();
  for (const id of behaviors) {
    const details = await client.call('behaviors', 'get_behavior_details', { behavior_id: id });
    const name = details.display_name ?? '';
    behaviorNames.set(id, name);
    if (name && !behaviorIds.has(name)) behaviorIds.set(name, id);
  }
  await loadMacros();
  await loadKeymap();
  if (!isPad()) await loadLayout();
  await loadKnob();
  showNotice();
}

async function setLocked(value) {
  locked = value;
  if (locked) resetTester();
  if (!locked) {
    try {
      await loadDevice();
    } catch (error) {
      showNotice(describeError(error), true);
    }
  }
  render();
}

function handleNotification(notification) {
  if (notification.core && 'lock_state_changed' in notification.core) {
    setLocked(notification.core.lock_state_changed !== LOCK_STATE.UNLOCKED);
  } else if (notification.keymap && 'unsaved_changes_status_changed' in notification.keymap) {
    setUnsaved(notification.keymap.unsaved_changes_status_changed);
  } else if (notification.custom?.custom_notification) {
    const { subsystem_index: index = 0, payload = new Uint8Array() } = notification.custom.custom_notification;
    if (testerOn && index === subsystems.get(KeyStream.SUBSYSTEM)) handleKeyStream(payload);
  }
}

async function closeConnection(message = 'Disconnected') {
  const activeTransport = transport;
  transport = undefined;
  client = undefined;
  controls = padControls;
  connectedName = PAD_NAME;
  selectedIndex = 0;
  locked = true;
  layers = [];
  unsaved = false;
  subsystems = new Map();
  macros = [];
  knobBindings = new Map();
  selectedKnob = null;
  resetTester();
  clearInterval(statusTimer);
  statusLedState = 'off';
  batteryStatus.hidden = true;
  bluetoothStatus.hidden = true;
  if (macroDialog.open) macroDialog.close();
  // Closing a half-open Bluetooth link can wait forever on Windows; don't let it.
  await Promise.race([activeTransport?.close(), new Promise(resolve => setTimeout(resolve, CLOSE_TIMEOUT_MS))]);
  statusDot.classList.remove('connected');
  statusText.textContent = message;
  connectButton.textContent = 'Connect USB';
  deviceName.textContent = 'No device';
  render();
}

// `open` is Transports.openSerial or Transports.openBluetooth.
async function connect(open) {
  if (transport) {
    // Leave the device quiet: it would otherwise keep streaming key presses.
    if (testerOn) try { await subsystemCall(KeyStream, 'disable_stream'); } catch {}
    await closeConnection();
    return;
  }
  if (connecting) return;
  connecting = true;
  connectButton.disabled = bluetoothButton.disabled = true;
  statusText.textContent = open === Transports.openBluetooth ? 'Looking for the device over Bluetooth...' : 'Connecting...';
  try {
    let activeClient;
    transport = await open(bytes => activeClient?.receive(bytes),
                           message => { if (client === activeClient) closeConnection(message); });
    activeClient = new StudioClient(bytes => transport.write(bytes));
    activeClient.onNotification = handleNotification;
    client = activeClient;

    const info = await client.call('core', 'get_device_info');
    statusDot.classList.add('connected');
    statusText.textContent = `Connected over ${transport.label}`;
    connectButton.textContent = 'Disconnect';
    connectedName = info.name || 'ZMK device';
    if (open === Transports.openSerial) {
      rememberDevice(connectedName, info.serial_number);
      const wanted = preferredDevice();
      if (wanted && connectedName !== wanted) {
        const got = deviceLabel(connectedName);
        await closeConnection();
        showNotice(`That was the ${got}, not the ${deviceLabel(wanted)}. Click Connect USB again to try the next ` +
                   `device, and check the ${deviceLabel(wanted)} is plugged in.`, true);
        return;
      }
    }
    deviceName.textContent = connectedName;
    if (!isPad()) {
      controls = [];
      selectedIndex = 0;
    }
    showNotice();
    render();
    await loadSubsystems();
    await refreshStatus();
    statusTimer = setInterval(refreshStatus, STATUS_POLL_MS);
    const lockState = await client.call('core', 'get_lock_state');
    await setLocked(lockState !== LOCK_STATE.UNLOCKED);
  } catch (error) {
    const absent = error.name === 'NotFoundError';
    const noAnswer = error.message === 'Timed out waiting for core.get_device_info';
    await closeConnection(absent ? 'No pad selected' : 'Connection failed');
    if (noAnswer && open === Transports.openBluetooth) {
      const hint = UNLOCK_HINTS[preferredDevice()] || 'press its Studio unlock key';
      showNotice('The device connected over Bluetooth but did not answer. Unlock it ' +
                 `(${hint.charAt(0).toLowerCase()}${hint.slice(1)}) and try again, or plug it in and use Connect USB.`, true);
    }
    else if (!absent) showNotice(describeError(error), true);
    else if (open === Transports.openBluetooth) {
      const hint = UNLOCK_HINTS[preferredDevice()] || 'press its Studio unlock key';
      showNotice('The device did not show up over Bluetooth. It only announces itself while unlocked: ' +
                 `${hint.charAt(0).toLowerCase()}${hint.slice(1)}, then try again. It also has to be paired with Windows.`, true);
    }
  } finally {
    connecting = false;
    connectButton.disabled = bluetoothButton.disabled = false;
  }
}

deviceSelect.addEventListener('change', () => writeStored(Transports.PREFERRED_DEVICE_KEY, deviceSelect.value));
connectButton.addEventListener('click', () => connect(Transports.openSerial));
bluetoothButton.addEventListener('click', () => connect(Transports.openBluetooth));
saveButton.addEventListener('click', async () => {
  try {
    const result = await client.call('keymap', 'save_changes');
    if (result.ok) {
      unsaved = false;
      showNotice('Saved to the device.');
    } else {
      showNotice('The device could not save the keymap.', true);
    }
  } catch (error) {
    showNotice(describeError(error), true);
  }
  render();
});
discardButton.addEventListener('click', async () => {
  if (!confirm('Discard unsaved changes and go back to what is saved on the device?')) return;
  try {
    await client.call('keymap', 'discard_changes');
    await loadKeymap();
    showNotice('Unsaved changes discarded.');
  } catch (error) {
    showNotice(describeError(error), true);
  }
  render();
});

document.querySelectorAll('[data-macro-mode]').forEach(tab => {
  tab.addEventListener('click', () => setMacroMode(tab.dataset.macroMode));
});
macroPreset.addEventListener('change', () => {
  if (!macroPreset.value) return;
  macroInput.value = macroPreset.value;
  const label = macroPreset.selectedOptions[0].textContent.replace(/ \(.*\)$/, '');
  if (!editingMacro && !macroName.value.trim()) macroName.value = label;
  macroPreset.selectedIndex = 0;
});
document.querySelector('#macroCancel').addEventListener('click', () => macroDialog.close());
testerToggle.addEventListener('click', () => setTester(!testerOn));
macroSave.addEventListener('click', saveMacro);
macroDelete.addEventListener('click', deleteMacro);

if (Transports.isNativeApp) {
  document.querySelector('#hostHint').textContent =
    'Pair the device in Android Bluetooth settings and unlock it before connecting.';
  deviceSelect.title = 'Which device Connect Bluetooth should look for';
}
render();
