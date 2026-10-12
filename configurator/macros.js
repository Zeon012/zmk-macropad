// Runtime macro support: the zmk-feature-runtime-macro RPC messages, and the
// conversion between what a person types and macro steps. Message layouts
// follow third-party/zmk-feature-runtime-macro/proto/cormoran/runtime_macro.
// Works as a classic browser script (window.MacropadMacros) and as a Node module.
(function (root) {
  'use strict';

  const Keys = typeof module !== 'undefined' && module.exports ? require('./keys.js') : root.MacropadKeys;

  const SUBSYSTEM = 'cormoran__runtime_macro';
  const Empty = {};
  const Binding = { behavior_id: [1, 'uint32'], param1: [2, 'uint32'], param2: [3, 'uint32'] };
  const Step = {
    down: [1, Binding],
    up: [2, Binding],
    tap: [3, Binding],
    delay: [4, { delay_ms: [1, 'uint32'] }],
    key_tap_sequence: [5, { packed_keys: [1, 'bytes'] }]
  };

  const Request = {
    list_macros: [1, Empty],
    get_macro: [2, { slot: [1, 'uint32'] }],
    set_macro_step_count: [4, { slot: [1, 'uint32'], step_count: [2, 'uint32'], persist: [3, 'bool'] }],
    get_macro_global_settings: [5, Empty],
    save_macros: [9, Empty],
    discard_macros: [10, Empty],
    append_macro_step: [11, { slot: [1, 'uint32'], step: [2, Step], persist: [3, 'bool'] }],
    create_macro: [12, { name: [1, 'string'], persist: [2, 'bool'] }],
    delete_macro: [13, { name: [1, 'string'] }]
  };

  const Response = {
    error: [1, { message: [1, 'string'] }],
    status: [2, { affected_count: [1, 'uint32'], message: [2, 'string'] }],
    list_macros: [3, {
      macros: [1, { slot: [1, 'uint32'], name: [2, 'string'], encoded_size: [3, 'uint32'],
                    has_unsaved_changes: [4, 'bool'] }, true],
      max_macro_bytes: [2, 'uint32'],
      max_name_length: [3, 'uint32']
    }],
    get_macro: [4, { macro: [1, { slot: [1, 'uint32'], name: [2, 'string'], steps: [3, Step, true],
                                  encoded_size: [4, 'uint32'] }] }],
    get_macro_global_settings: [5, { settings: [1, {
      tap_ms: [1, 'uint32'], max_entries: [2, 'uint32'], key_press_behavior_id: [3, 'uint32'],
      pool_bytes_total: [4, 'uint32'], pool_bytes_used: [5, 'uint32']
    }] }]
  };

  // A text step packs one key per byte: bit 7 is Shift, bits 0-6 the HID
  // keyboard usage. The firmware accepts at most 64 keys per step.
  const PACKED_SHIFT = 0x80;
  const MAX_PACKED_KEYS = 64;
  const MAX_TEXT_LENGTH = 200;

  // US layout: character -> [usage id, needs Shift].
  const charKeys = new Map();
  for (let i = 0; i < 26; i++) {
    charKeys.set(String.fromCharCode(97 + i), [0x04 + i, false]);
    charKeys.set(String.fromCharCode(65 + i), [0x04 + i, true]);
  }
  '1234567890'.split('').forEach((char, i) => charKeys.set(char, [0x1e + i, false]));
  '!@#$%^&*()'.split('').forEach((char, i) => charKeys.set(char, [0x1e + i, true]));
  [[' ', 0x2c], ['\n', 0x28], ['\t', 0x2b]].forEach(([char, id]) => charKeys.set(char, [id, false]));
  [['-', '_', 0x2d], ['=', '+', 0x2e], ['[', '{', 0x2f], [']', '}', 0x30], ['\\', '|', 0x31],
   [';', ':', 0x33], ["'", '"', 0x34], ['`', '~', 0x35], [',', '<', 0x36], ['.', '>', 0x37],
   ['/', '?', 0x38]].forEach(([plain, shifted, id]) => {
    charKeys.set(plain, [id, false]);
    charKeys.set(shifted, [id, true]);
  });
  const charForPacked = new Map([...charKeys].map(([char, [id, shift]]) => [id | (shift ? PACKED_SHIFT : 0), char]));

  // Each converter returns { steps } or { error }.

  function textToSteps(text) {
    if (!text) return { error: 'Type the text the macro should enter.' };
    if (text.length > MAX_TEXT_LENGTH) return { error: `Keep the text to ${MAX_TEXT_LENGTH} characters.` };
    const packed = [];
    for (const char of text) {
      const entry = charKeys.get(char);
      if (!entry) return { error: `"${char}" cannot be typed by a macro.` };
      packed.push(entry[0] | (entry[1] ? PACKED_SHIFT : 0));
    }
    const steps = [];
    for (let i = 0; i < packed.length; i += MAX_PACKED_KEYS) {
      steps.push({ key_tap_sequence: { packed_keys: Uint8Array.from(packed.slice(i, i + MAX_PACKED_KEYS)) } });
    }
    return { steps };
  }

  // "Ctrl+Shift+S, 150ms, Win+R": combos and pauses separated by commas.
  function comboToSteps(sequence, keyPressBehaviorId) {
    const items = sequence.split(',').map(item => item.trim()).filter(Boolean);
    if (!items.length) return { error: 'Type at least one key combo.' };
    const steps = [];
    for (const item of items) {
      const pause = /^(\d+)\s*ms$/i.exec(item);
      if (pause) {
        steps.push({ delay: { delay_ms: Number(pause[1]) } });
        continue;
      }
      const parts = item.split('+').map(part => part.trim());
      if (parts.some(part => !part)) return { error: `"${item}" is not a key combo.` };
      const keyName = parts.pop();
      let mods = 0;
      for (const part of parts) {
        const bit = Keys.modifierBit(part);
        if (!bit) return { error: `"${part}" is not a modifier. Use Ctrl, Shift, Alt or Win.` };
        mods |= bit;
      }
      const code = Keys.codeForName(keyName);
      if (code === null) return { error: `"${keyName}" is not a key name.` };
      steps.push({ tap: { behavior_id: keyPressBehaviorId, param1: Keys.withMods(code, mods), param2: 0 } });
    }
    return { steps };
  }

  // Turns stored steps back into something the editor can show:
  // { mode: 'text' | 'combo', value }, or { mode: 'custom' } when the macro
  // uses steps this editor does not write (press-and-hold, other behaviors).
  function stepsToEditable(steps, keyPressBehaviorId) {
    if (!steps.length) return { mode: 'text', value: '' };
    if (steps.every(step => step.key_tap_sequence)) {
      const chars = steps.flatMap(step => [...(step.key_tap_sequence.packed_keys || [])].map(key => charForPacked.get(key)));
      if (chars.every(char => char !== undefined)) return { mode: 'text', value: chars.join('') };
    }
    const items = [];
    for (const step of steps) {
      if (step.delay) {
        items.push(`${step.delay.delay_ms ?? 0}ms`);
      } else if (step.tap && (step.tap.behavior_id ?? 0) === keyPressBehaviorId) {
        const code = step.tap.param1 ?? 0;
        items.push([...Keys.modifierNames(Keys.modsOf(code)), Keys.keyName(code)].join('+'));
      } else if (step.key_tap_sequence) {
        for (const key of step.key_tap_sequence.packed_keys || []) {
          const name = Keys.keyName(Keys.usage(Keys.PAGE_KEYBOARD, key & ~PACKED_SHIFT));
          items.push(key & PACKED_SHIFT ? `Shift+${name}` : name);
        }
      } else {
        return { mode: 'custom' };
      }
    }
    return { mode: 'combo', value: items.join(', ') };
  }

  // The old editor's quick links: the Win+X menu, a pause for it to open, then a letter.
  const presets = [
    ['Terminal', 'Win+X, 250ms, I'], ['Admin Terminal', 'Win+X, 250ms, A'],
    ['Task Manager', 'Win+X, 250ms, T'], ['Disk Management', 'Win+X, 250ms, K'],
    ['Device Manager', 'Win+X, 250ms, M'], ['Computer Management', 'Win+X, 250ms, G'],
    ['File Explorer', 'Win+X, 250ms, E'], ['Run', 'Win+X, 250ms, R']
  ].map(([name, combo]) => ({ name, combo }));

  const api = { SUBSYSTEM, Request, Response, MAX_TEXT_LENGTH, textToSteps, comboToSteps, stepsToEditable, presets };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.MacropadMacros = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
