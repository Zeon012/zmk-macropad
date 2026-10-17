/*
 * VIA's raw HID protocol (QMK quantum/via.c, protocol version 12): 32-byte
 * reports, each answered by sending the same report back with the results
 * filled in, or with byte 0 set to 0xFF for a command this firmware lacks.
 *
 * VIA addresses keys by matrix row and column; the selected physical layout's
 * matrix transform turns those into ZMK key positions. Unused matrix spots
 * read as KC_NO and ignore writes.
 *
 * Reading is always allowed. With MACROPAD_VIA_REQUIRE_UNLOCK, changes only
 * go through while ZMK Studio is unlocked, the same rule the configurator has.
 *
 * Reports arrive in the USB or Bluetooth stack's context (via_usb_hid.c,
 * via_ble_hid.c), so they are copied and handled on the system work queue,
 * and each reply goes back over the connection its request came in on. (A
 * reply sent both ways made VIA read every answer twice.)
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/keymap.h>
#include <zmk/matrix_transform.h>
#include <zmk/physical_layouts.h>

#if IS_ENABLED(CONFIG_ZMK_STUDIO)
#include <zmk/studio/core.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_RUNTIME_SENSOR_ROTATE)
#include <zmk/behaviors/runtime_sensor_rotate.h>
#endif

#include "via.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define VIA_PROTOCOL_VERSION 0x000C
#define REPORT_SIZE VIA_REPORT_SIZE
#define MAX_DATA (REPORT_SIZE - 4)
#define KEYMAP_SAVE_DELAY_MS 1000

/* VIA's matrix: the rows and columns of the chosen physical layout's matrix transform. */
#define VIA_TRANSFORM DT_PHANDLE(DT_CHOSEN(zmk_physical_layout), transform)
#define VIA_ROWS DT_PROP(VIA_TRANSFORM, rows)
#define VIA_COLS DT_PROP(VIA_TRANSFORM, columns)

enum via_command {
    id_get_protocol_version = 0x01,
    id_get_keyboard_value = 0x02,
    id_set_keyboard_value = 0x03,
    id_dynamic_keymap_get_keycode = 0x04,
    id_dynamic_keymap_set_keycode = 0x05,
    id_dynamic_keymap_reset = 0x06,
    id_custom_set_value = 0x07,
    id_custom_get_value = 0x08,
    id_custom_save = 0x09,
    id_dynamic_keymap_macro_get_count = 0x0C,
    id_dynamic_keymap_macro_get_buffer_size = 0x0D,
    id_dynamic_keymap_macro_get_buffer = 0x0E,
    id_dynamic_keymap_macro_set_buffer = 0x0F,
    id_dynamic_keymap_macro_reset = 0x10,
    id_dynamic_keymap_get_layer_count = 0x11,
    id_dynamic_keymap_get_buffer = 0x12,
    id_dynamic_keymap_set_buffer = 0x13,
    id_dynamic_keymap_get_encoder = 0x14,
    id_dynamic_keymap_set_encoder = 0x15,
    id_unhandled = 0xFF,
};

enum via_keyboard_value {
    id_uptime = 0x01,
    id_layout_options = 0x02,
    id_switch_matrix_state = 0x03,
    id_firmware_version = 0x04,
};

struct queued_report {
    uint8_t report[REPORT_SIZE];
    enum via_transport transport;
    void *conn;
};

K_MSGQ_DEFINE(report_queue, sizeof(struct queued_report), 4, 4);

static bool may_change(void) {
#if IS_ENABLED(CONFIG_MACROPAD_VIA_REQUIRE_UNLOCK)
    return zmk_studio_core_get_lock_state() == ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED;
#else
    return true;
#endif
}

static void put_u32(uint8_t *out, uint32_t value) {
    out[0] = value >> 24;
    out[1] = value >> 16;
    out[2] = value >> 8;
    out[3] = value;
}

/* The ZMK key position at a VIA matrix spot, or -1 if none. */
static int position_at(uint8_t row, uint8_t col) {
    struct zmk_physical_layout const *const *layouts;
    size_t count = zmk_physical_layouts_get_list(&layouts);
    int selected = zmk_physical_layouts_get_selected();
    if (selected < 0 || selected >= count || row >= VIA_ROWS || col >= VIA_COLS) {
        return -1;
    }
    int32_t position =
        zmk_matrix_transform_row_column_to_position(layouts[selected]->matrix_transform, row, col);
    return position < 0 ? -1 : position;
}

static uint16_t get_keycode(uint8_t layer, uint8_t row, uint8_t col) {
    zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(layer);
    int position = position_at(row, col);
    if (id == UINT8_MAX || position < 0) {
        return VIA_KC_NO;
    }
    const struct zmk_behavior_binding *binding = zmk_keymap_get_layer_binding_at_idx(id, position);
    return binding ? via_keycode_from_binding(binding) : VIA_KC_NO;
}

static void save_keymap_handler(struct k_work *work) {
    int err = zmk_keymap_save_changes();
    if (err < 0) {
        LOG_ERR("Saving the keymap changed from VIA failed (%d)", err);
    }
}

static K_WORK_DELAYABLE_DEFINE(save_keymap_work, save_keymap_handler);

static void set_keycode(uint8_t layer, uint8_t row, uint8_t col, uint16_t keycode) {
    zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(layer);
    int position = position_at(row, col);
    struct zmk_behavior_binding binding;
    if (id == UINT8_MAX || position < 0 || !via_keycode_to_binding(keycode, &binding)) {
        return;
    }
    if (zmk_keymap_set_layer_binding_at_idx(id, position, binding) == 0) {
        /* VIA has no Save button: changes are kept once it stops sending them. */
        k_work_reschedule(&save_keymap_work, K_MSEC(KEYMAP_SAVE_DELAY_MS));
    }
}

/* The keymap as VIA sees it: [layer][row][col] keycodes, two bytes each, high byte first. */
static void get_keymap_bytes(uint16_t offset, uint8_t size, uint8_t *out) {
    for (uint8_t i = 0; i < size; i++) {
        uint16_t key = (offset + i) / 2;
        uint16_t per_layer = VIA_ROWS * VIA_COLS;
        uint8_t layer = key / per_layer, spot = key % per_layer;
        uint16_t keycode = layer < ZMK_KEYMAP_LAYERS_LEN
                               ? get_keycode(layer, spot / VIA_COLS, spot % VIA_COLS)
                               : VIA_KC_NO;
        out[i] = ((offset + i) % 2 == 0) ? keycode >> 8 : keycode & 0xFF;
    }
}

static void set_keymap_bytes(uint16_t offset, uint8_t size, const uint8_t *data) {
    uint16_t per_layer = VIA_ROWS * VIA_COLS;
    /* VIA writes whole keycodes; a half one at either end is completed from the current value. */
    for (uint16_t key = offset / 2; key * 2 < offset + size; key++) {
        uint8_t bytes[2];
        get_keymap_bytes(key * 2, 2, bytes);
        for (uint8_t b = 0; b < 2; b++) {
            uint16_t at = key * 2 + b;
            if (at >= offset && at < offset + size) {
                bytes[b] = data[at - offset];
            }
        }
        uint8_t layer = key / per_layer, spot = key % per_layer;
        if (layer < ZMK_KEYMAP_LAYERS_LEN) {
            set_keycode(layer, spot / VIA_COLS, spot % VIA_COLS,
                        (bytes[0] << 8) | bytes[1]);
        }
    }
}

#if IS_ENABLED(CONFIG_ZMK_RUNTIME_SENSOR_ROTATE)

static uint16_t get_encoder(uint8_t layer, uint8_t encoder, bool clockwise) {
    struct runtime_sensor_rotate_layer_bindings bindings;
    zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(layer);
    if (id == UINT8_MAX || zmk_runtime_sensor_rotate_get_layer_bindings(encoder, id, &bindings)) {
        return VIA_KC_NO;
    }
    const struct runtime_sensor_rotate_binding *turn =
        clockwise ? &bindings.cw_binding : &bindings.ccw_binding;
    const char *name = zmk_behavior_find_behavior_name_from_local_id(turn->behavior_local_id);
    if (!name) {
        return VIA_KC_NO;
    }
    struct zmk_behavior_binding binding = {
        .behavior_dev = name, .param1 = turn->param1, .param2 = turn->param2};
    return via_keycode_from_binding(&binding);
}

static void set_encoder(uint8_t layer, uint8_t encoder, bool clockwise, uint16_t keycode) {
    struct runtime_sensor_rotate_layer_bindings bindings;
    struct zmk_behavior_binding binding;
    zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(layer);
    if (id == UINT8_MAX || zmk_runtime_sensor_rotate_get_layer_bindings(encoder, id, &bindings) ||
        !via_keycode_to_binding(keycode, &binding)) {
        return;
    }
    struct runtime_sensor_rotate_binding *turn =
        clockwise ? &bindings.cw_binding : &bindings.ccw_binding;
    turn->behavior_local_id = zmk_behavior_get_local_id(binding.behavior_dev);
    turn->param1 = binding.param1;
    turn->param2 = binding.param2;
    zmk_runtime_sensor_rotate_set_layer_bindings(encoder, id, &bindings);
}

#endif

static void handle_report(uint8_t *report) {
    uint8_t *command = &report[0];
    uint8_t *data = &report[1];

    switch (*command) {
    case id_get_protocol_version:
        data[0] = VIA_PROTOCOL_VERSION >> 8;
        data[1] = VIA_PROTOCOL_VERSION & 0xFF;
        break;
    case id_get_keyboard_value:
        switch (data[0]) {
        case id_uptime:
            put_u32(&data[1], k_uptime_get_32());
            break;
        case id_layout_options:
        case id_firmware_version:
            put_u32(&data[1], 0);
            break;
        case id_switch_matrix_state:
            /* VIA's key tester; QMK also leaves this empty unless built insecure. */
            memset(&data[2], 0, REPORT_SIZE - 3);
            break;
        default:
            *command = id_unhandled;
        }
        break;
    case id_set_keyboard_value:
        if (data[0] != id_layout_options) {
            *command = id_unhandled;
        }
        break;
    case id_dynamic_keymap_get_keycode: {
        uint16_t keycode = get_keycode(data[0], data[1], data[2]);
        data[3] = keycode >> 8;
        data[4] = keycode & 0xFF;
        break;
    }
    case id_dynamic_keymap_set_keycode:
        if (!may_change()) {
            *command = id_unhandled;
            break;
        }
        set_keycode(data[0], data[1], data[2], (data[3] << 8) | data[4]);
        break;
    case id_dynamic_keymap_reset:
        if (!may_change()) {
            *command = id_unhandled;
            break;
        }
        zmk_keymap_reset_settings();
        break;
    case id_custom_set_value:
    case id_custom_get_value:
    case id_custom_save:
        /* No lighting or custom menus in the VIA definition. */
        *command = id_unhandled;
        break;
    case id_dynamic_keymap_macro_get_count:
        data[0] = via_macro_count();
        break;
    case id_dynamic_keymap_macro_get_buffer_size: {
        uint16_t size = via_macro_buffer_size();
        data[0] = size >> 8;
        data[1] = size & 0xFF;
        break;
    }
    case id_dynamic_keymap_macro_get_buffer:
        via_macro_get_buffer((data[0] << 8) | data[1], MIN(data[2], MAX_DATA), &data[3]);
        break;
    case id_dynamic_keymap_macro_set_buffer:
        if (!may_change()) {
            *command = id_unhandled;
            break;
        }
        via_macro_set_buffer((data[0] << 8) | data[1], MIN(data[2], MAX_DATA), &data[3]);
        break;
    case id_dynamic_keymap_macro_reset:
        if (!may_change()) {
            *command = id_unhandled;
            break;
        }
        via_macro_reset();
        break;
    case id_dynamic_keymap_get_layer_count:
        data[0] = ZMK_KEYMAP_LAYERS_LEN;
        break;
    case id_dynamic_keymap_get_buffer:
        get_keymap_bytes((data[0] << 8) | data[1], MIN(data[2], MAX_DATA), &data[3]);
        break;
    case id_dynamic_keymap_set_buffer:
        if (!may_change()) {
            *command = id_unhandled;
            break;
        }
        set_keymap_bytes((data[0] << 8) | data[1], MIN(data[2], MAX_DATA), &data[3]);
        break;
#if IS_ENABLED(CONFIG_ZMK_RUNTIME_SENSOR_ROTATE)
    case id_dynamic_keymap_get_encoder: {
        uint16_t keycode = get_encoder(data[0], data[1], data[2] != 0);
        data[3] = keycode >> 8;
        data[4] = keycode & 0xFF;
        break;
    }
    case id_dynamic_keymap_set_encoder:
        if (!may_change()) {
            *command = id_unhandled;
            break;
        }
        set_encoder(data[0], data[1], data[2] != 0, (data[3] << 8) | data[4]);
        break;
#endif
    default:
        *command = id_unhandled;
    }
}

static void report_work_handler(struct k_work *work) {
    struct queued_report item;
    while (k_msgq_get(&report_queue, &item, K_NO_WAIT) == 0) {
        handle_report(item.report);
#if IS_ENABLED(CONFIG_MACROPAD_VIA_BLE)
        if (item.transport == VIA_TRANSPORT_BLE) {
            via_ble_send_report(item.conn, item.report);
            continue;
        }
#endif
        via_usb_send_report(item.report);
    }
}

static K_WORK_DEFINE(report_work, report_work_handler);

void via_receive_report(const uint8_t *data, size_t length, enum via_transport transport,
                        void *conn) {
    struct queued_report item = {.transport = transport, .conn = conn};
    memcpy(item.report, data, MIN(length, REPORT_SIZE));
    if (k_msgq_put(&report_queue, &item, K_NO_WAIT) == 0) {
        k_work_submit(&report_work);
        return;
    }
    LOG_WRN("VIA report dropped: the queue is full");
#if IS_ENABLED(CONFIG_MACROPAD_VIA_BLE)
    if (transport == VIA_TRANSPORT_BLE) {
        via_ble_release(conn);
    }
#endif
}
