// Messages for the knob and key-stream firmware modules. Layouts follow
// third-party/zmk-behavior-runtime-sensor-rotate/proto/cormoran/rsr/custom.proto
// and third-party/zmk-feature-input-stream/proto/zmk/input_stream/input_stream.proto.
// Works as a classic browser script (window.MacropadSubsystems) and as a Node module.
(function (root) {
  'use strict';

  const Empty = {};
  const ErrorResponse = { message: [1, 'string'] };

  // Knob: one clockwise and one counter-clockwise binding per layer id.
  const KnobBinding = { behavior_id: [1, 'uint32'], param1: [2, 'uint32'], param2: [3, 'uint32'], tap_ms: [4, 'uint32'] };
  const SetKnobBinding = { sensor_index: [1, 'uint32'], layer: [2, 'uint32'], binding: [3, KnobBinding] };
  const Knob = {
    SUBSYSTEM: 'cormoran_rsr',
    DEFAULT_TAP_MS: 5,
    Request: {
      set_layer_cw_binding: [1, SetKnobBinding],
      set_layer_ccw_binding: [2, SetKnobBinding],
      get_all_layer_bindings: [3, { sensor_index: [1, 'uint32'] }]
    },
    Response: {
      error: [1, ErrorResponse],
      set_layer_cw_binding: [2, { success: [1, 'bool'] }],
      set_layer_ccw_binding: [3, { success: [1, 'bool'] }],
      get_all_layer_bindings: [4, {
        bindings: [1, { layer: [1, 'uint32'], cw_binding: [2, KnobBinding], ccw_binding: [3, KnobBinding] }, true]
      }]
    }
  };

  // Key stream: the pad reports each key press and release while enabled.
  const KeyStream = {
    SUBSYSTEM: 'zmk__input_stream',
    Request: { enable_stream: [1, Empty], disable_stream: [2, Empty] },
    Response: { error: [1, ErrorResponse], ok: [2, Empty] },
    Notification: {
      key_event: [1, { position: [1, 'uint32'], pressed: [2, 'bool'], behavior_id: [3, 'uint32'],
                       param1: [4, 'uint32'], param2: [5, 'uint32'] }],
      layer_change: [2, { layer_index: [1, 'uint32'] }]
    }
  };

  const api = { Knob, KeyStream };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.MacropadSubsystems = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
