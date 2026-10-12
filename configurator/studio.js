// ZMK Studio RPC client: protobuf codec, serial framing and request/response
// matching. Message layouts follow third-party/zmk-studio-messages/proto/zmk.
// Works as a classic browser script (window.ZmkStudio) and as a Node module.
(function (root) {
  'use strict';

  // ---- Message schemas: field name -> [field number, type, repeated?] ----
  // A type is a scalar name or another schema object.

  const Empty = {};
  const BehaviorBinding = { behavior_id: [1, 'sint32'], param1: [2, 'uint32'], param2: [3, 'uint32'] };
  const Layer = { id: [1, 'uint32'], name: [2, 'string'], bindings: [3, BehaviorBinding, true] };
  const Keymap = {
    layers: [1, Layer, true],
    available_layers: [2, 'uint32'],
    max_layer_name_length: [3, 'uint32']
  };
  const ParameterDescription = {
    name: [1, 'string'],
    nil: [2, Empty],
    constant: [3, 'uint32'],
    range: [4, { min: [1, 'int32'], max: [2, 'int32'] }],
    hid_usage: [5, { keyboard_max: [1, 'uint32'], consumer_max: [2, 'uint32'] }],
    layer_id: [6, Empty]
  };
  const ParameterSet = { param1: [1, ParameterDescription, true], param2: [2, ParameterDescription, true] };
  // Sizes and positions are in hundredths of a key unit.
  const KeyPhysicalAttrs = { width: [1, 'sint32'], height: [2, 'sint32'], x: [3, 'sint32'], y: [4, 'sint32'] };
  const CustomCall = { subsystem_index: [1, 'uint32'], payload: [2, 'bytes'] };

  const Request = {
    request_id: [1, 'uint32'],
    core: [3, {
      get_device_info: [1, 'bool'],
      get_lock_state: [2, 'bool'],
      lock: [3, 'bool'],
      reset_settings: [4, 'bool']
    }],
    behaviors: [4, {
      list_all_behaviors: [1, 'bool'],
      get_behavior_details: [2, { behavior_id: [1, 'uint32'] }]
    }],
    keymap: [5, {
      get_keymap: [1, 'bool'],
      set_layer_binding: [2, {
        layer_id: [1, 'uint32'],
        key_position: [2, 'int32'],
        binding: [3, BehaviorBinding]
      }],
      check_unsaved_changes: [3, 'bool'],
      save_changes: [4, 'bool'],
      discard_changes: [5, 'bool'],
      get_physical_layouts: [6, 'bool']
    }],
    custom: [100, {
      list_custom_subsystems: [1, Empty],
      call: [2, CustomCall]
    }]
  };

  const RequestResponse = {
    request_id: [1, 'uint32'],
    meta: [2, { no_response: [1, 'bool'], simple_error: [2, 'uint32'] }],
    core: [3, {
      get_device_info: [1, { name: [1, 'string'], serial_number: [2, 'bytes'] }],
      get_lock_state: [2, 'uint32'],
      reset_settings: [4, 'bool']
    }],
    behaviors: [4, {
      list_all_behaviors: [1, { behaviors: [1, 'uint32', true] }],
      get_behavior_details: [2, {
        id: [1, 'uint32'],
        display_name: [2, 'string'],
        metadata: [3, ParameterSet, true]
      }]
    }],
    keymap: [5, {
      get_keymap: [1, Keymap],
      set_layer_binding: [2, 'uint32'],
      check_unsaved_changes: [3, 'bool'],
      save_changes: [4, { ok: [1, 'bool'], err: [2, 'uint32'] }],
      discard_changes: [5, 'bool'],
      get_physical_layouts: [6, {
        active_layout_index: [1, 'uint32'],
        layouts: [2, { name: [1, 'string'], keys: [2, KeyPhysicalAttrs, true] }, true]
      }]
    }],
    custom: [100, {
      list_custom_subsystems: [1, {
        subsystems: [1, { index: [1, 'uint32'], identifier: [2, 'string'], ui_url: [3, 'string', true] }, true]
      }],
      call: [2, CustomCall]
    }]
  };

  const Notification = {
    core: [2, { lock_state_changed: [1, 'uint32'] }],
    keymap: [5, { unsaved_changes_status_changed: [1, 'bool'] }],
    custom: [100, { custom_notification: [1, CustomCall] }]
  };

  const Response = { request_response: [1, RequestResponse], notification: [2, Notification] };

  const META_ERRORS = ['GENERIC', 'UNLOCK_REQUIRED', 'RPC_NOT_FOUND', 'MSG_DECODE_FAILED', 'MSG_ENCODE_FAILED'];
  const LOCK_STATE = { LOCKED: 0, UNLOCKED: 1 };

  // ---- Protobuf wire format ----

  const WIRE_VARINT = 0;
  const WIRE_FIXED64 = 1;
  const WIRE_BYTES = 2;
  const WIRE_FIXED32 = 5;
  const VARINT_TYPES = ['uint32', 'int32', 'sint32', 'bool'];
  const textEncoder = new TextEncoder();
  const textDecoder = new TextDecoder();

  function pushVarint(out, value) {
    let v = BigInt.asUintN(64, BigInt(value));
    while (v > 0x7fn) {
      out.push(Number(v & 0x7fn) | 0x80);
      v >>= 7n;
    }
    out.push(Number(v));
  }

  function scalarToVarint(type, value) {
    if (type === 'bool') return value ? 1 : 0;
    if (type === 'sint32') return BigInt.asUintN(32, (BigInt(value) << 1n) ^ (BigInt(value) >> 31n));
    return value; // uint32, and int32 (negative values sign-extend to 64 bits)
  }

  function encodeValue(out, number, type, value) {
    if (VARINT_TYPES.includes(type)) {
      pushVarint(out, (number << 3) | WIRE_VARINT);
      pushVarint(out, scalarToVarint(type, value));
      return;
    }
    let bytes;
    if (type === 'string') bytes = textEncoder.encode(value);
    else if (type === 'bytes') bytes = value;
    else bytes = encode(type, value);
    pushVarint(out, (number << 3) | WIRE_BYTES);
    pushVarint(out, bytes.length);
    for (const byte of bytes) out.push(byte);
  }

  // Every key present on `message` is written, including zero values, which is
  // what the oneof members used by the protocol need.
  function encode(schema, message) {
    const out = [];
    for (const [name, [number, type, repeated]] of Object.entries(schema)) {
      const value = message[name];
      if (value === undefined || value === null) continue;
      for (const item of repeated ? value : [value]) encodeValue(out, number, type, item);
    }
    return Uint8Array.from(out);
  }

  function readVarint(bytes, state) {
    let result = 0n;
    let shift = 0n;
    for (;;) {
      if (state.pos >= bytes.length) throw new Error('Truncated varint');
      const byte = bytes[state.pos++];
      result |= BigInt(byte & 0x7f) << shift;
      if (!(byte & 0x80)) return result;
      shift += 7n;
      if (shift > 63n) throw new Error('Varint too long');
    }
  }

  function varintToScalar(type, raw) {
    if (type === 'bool') return raw !== 0n;
    if (type === 'int32') return Number(BigInt.asIntN(32, raw));
    if (type === 'sint32') {
      const value = BigInt.asUintN(32, raw);
      return Number((value >> 1n) ^ -(value & 1n));
    }
    return Number(BigInt.asUintN(32, raw));
  }

  // Only fields present on the wire appear in the result, except repeated
  // fields, which are always arrays. Callers supply defaults for the rest.
  function decode(schema, bytes) {
    const byNumber = new Map();
    const message = {};
    for (const [name, [number, type, repeated]] of Object.entries(schema)) {
      byNumber.set(number, [name, type, repeated]);
      if (repeated) message[name] = [];
    }
    const state = { pos: 0 };
    while (state.pos < bytes.length) {
      const key = Number(readVarint(bytes, state));
      const wire = key & 7;
      const field = byNumber.get(key >>> 3);
      if (wire === WIRE_VARINT) {
        const raw = readVarint(bytes, state);
        if (!field) continue;
        const [name, type, repeated] = field;
        const value = varintToScalar(type, raw);
        if (repeated) message[name].push(value);
        else message[name] = value;
      } else if (wire === WIRE_BYTES) {
        const length = Number(readVarint(bytes, state));
        const chunk = bytes.subarray(state.pos, state.pos + length);
        if (chunk.length !== length) throw new Error('Truncated field');
        state.pos += length;
        if (!field) continue;
        const [name, type, repeated] = field;
        if (repeated && VARINT_TYPES.includes(type)) {
          // Packed repeated scalars.
          const packed = { pos: 0 };
          while (packed.pos < chunk.length) message[name].push(varintToScalar(type, readVarint(chunk, packed)));
          continue;
        }
        let value;
        if (type === 'string') value = textDecoder.decode(chunk);
        else if (type === 'bytes') value = chunk.slice();
        else value = decode(type, chunk);
        if (repeated) message[name].push(value);
        else message[name] = value;
      } else if (wire === WIRE_FIXED64) {
        state.pos += 8;
      } else if (wire === WIRE_FIXED32) {
        state.pos += 4;
      } else {
        throw new Error(`Unsupported wire type ${wire}`);
      }
    }
    return message;
  }

  // ---- Serial framing: SOF, payload with escapes, EOF ----

  const SOF = 0xab;
  const ESC = 0xac;
  const EOF = 0xad;

  function frame(payload) {
    const out = [SOF];
    for (const byte of payload) {
      if (byte === SOF || byte === ESC || byte === EOF) out.push(ESC);
      out.push(byte);
    }
    out.push(EOF);
    return Uint8Array.from(out);
  }

  class FrameDecoder {
    constructor() {
      this.buffer = null;
      this.escaped = false;
    }

    // Returns the payloads completed by `chunk`.
    push(chunk) {
      const frames = [];
      for (const byte of chunk) {
        if (this.buffer === null) {
          if (byte === SOF) this.buffer = [];
          continue;
        }
        if (this.escaped) {
          this.buffer.push(byte);
          this.escaped = false;
        } else if (byte === ESC) {
          this.escaped = true;
        } else if (byte === EOF) {
          frames.push(Uint8Array.from(this.buffer));
          this.buffer = null;
        } else if (byte === SOF) {
          this.buffer = [];
        } else {
          this.buffer.push(byte);
        }
      }
      return frames;
    }
  }

  function encodeRequest(requestId, subsystem, type, value) {
    return frame(encode(Request, { request_id: requestId, [subsystem]: { [type]: value } }));
  }

  function decodeResponse(payload) {
    return decode(Response, payload);
  }

  class StudioError extends Error {
    constructor(code) {
      super(`Device returned ${code}`);
      this.name = 'StudioError';
      this.code = code;
    }
  }

  // ---- Client ----

  // `write` is an async function taking a Uint8Array. Feed incoming bytes to
  // `receive`. Requests are sent one at a time, as the firmware expects.
  class StudioClient {
    constructor(write, { timeoutMs = 5000 } = {}) {
      this.write = write;
      this.timeoutMs = timeoutMs;
      this.decoder = new FrameDecoder();
      this.nextRequestId = 1;
      this.pending = null;
      this.queue = Promise.resolve();
      this.onNotification = null;
    }

    receive(chunk) {
      for (const payload of this.decoder.push(chunk)) {
        let response;
        try {
          response = decodeResponse(payload);
        } catch (error) {
          console.warn('Dropped an undecodable Studio frame', error);
          continue;
        }
        if (response.notification) {
          if (this.onNotification) this.onNotification(response.notification);
        } else if (response.request_response && this.pending &&
                   (response.request_response.request_id ?? 0) === this.pending.id) {
          const { resolve } = this.pending;
          this.pending = null;
          resolve(response.request_response);
        }
      }
    }

    // Resolves with the value of the named response member, or rejects with a
    // StudioError (for example code 'UNLOCK_REQUIRED').
    call(subsystem, type, value = true) {
      const run = async () => {
        const id = this.nextRequestId++;
        const reply = new Promise((resolve, reject) => {
          const timer = setTimeout(() => {
            if (this.pending && this.pending.id === id) this.pending = null;
            reject(new Error(`Timed out waiting for ${subsystem}.${type}`));
          }, this.timeoutMs);
          this.pending = { id, resolve: response => { clearTimeout(timer); resolve(response); } };
        });
        await this.write(encodeRequest(id, subsystem, type, value));
        const response = await reply;
        if (response.meta) {
          if ('simple_error' in response.meta) throw new StudioError(META_ERRORS[response.meta.simple_error] || 'GENERIC');
          throw new StudioError('NO_RESPONSE');
        }
        const body = response[subsystem];
        if (!body || !(type in body)) throw new Error(`Unexpected reply to ${subsystem}.${type}`);
        return body[type];
      };
      const result = this.queue.then(run, run);
      this.queue = result.catch(() => {});
      return result;
    }
  }

  const api = {
    StudioClient, StudioError, FrameDecoder, LOCK_STATE,
    encode, decode, frame, encodeRequest, decodeResponse,
    schemas: { Request, Response, BehaviorBinding, Keymap }
  };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.ZmkStudio = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
