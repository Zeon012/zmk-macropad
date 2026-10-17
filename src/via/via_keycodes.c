/*
 * Translation between QMK's 16-bit keycodes (what VIA reads and writes) and
 * ZMK behavior bindings. Covered: plain keys, keys with modifiers, media and
 * system keys, layer keys (momentary, toggle, to), VIA macros, bootloader and
 * reset, and the macropad's Bluetooth/output/unlock keys as VIA custom
 * keycodes. Anything else shows in VIA as "ZMK" and is left alone.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <dt-bindings/zmk/bt.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/modifiers.h>
#include <dt-bindings/zmk/outputs.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>

#include "via.h"

/* QMK keycode ranges (quantum/keycodes.h, keycodes version 0.0.9). */
#define QK_BASIC_MAX 0x00FF
#define QK_MODS 0x0100
#define QK_MODS_MAX 0x1FFF
#define QK_TO 0x5200
#define QK_MOMENTARY 0x5220
#define QK_TOGGLE_LAYER 0x5260
#define QK_LAYER_MASK 0x001F
#define QK_MACRO 0x7700
#define QK_MACRO_MAX 0x777F
#define QK_BOOTLOADER 0x7C00
#define QK_REBOOT 0x7C01
#define QK_KB 0x7E00

/* QMK puts modifiers in bits 8-12: ctrl, shift, alt, gui, and "right hand". */
#define QMK_MOD_RIGHT 0x10

/* VIA custom keycodes, in the order of customKeycodes in the VIA definition. */
enum via_custom {
    VIA_CUSTOM_BT_1,
    VIA_CUSTOM_BT_2,
    VIA_CUSTOM_BT_3,
    VIA_CUSTOM_BT_CLEAR,
    VIA_CUSTOM_OUTPUT_TOGGLE,
    VIA_CUSTOM_STUDIO_UNLOCK,
    VIA_CUSTOM_ZMK_OTHER,
};

/* QMK basic keycodes 0xA5-0xBE stand for system and consumer usages. */
struct usage_alias {
    uint8_t qmk;
    uint8_t page;
    uint16_t id;
};

static const struct usage_alias usage_aliases[] = {
    {0xA5, HID_USAGE_GD, 0x81},          /* system power */
    {0xA6, HID_USAGE_GD, 0x82},          /* system sleep */
    {0xA7, HID_USAGE_GD, 0x83},          /* system wake */
    {0xA8, HID_USAGE_CONSUMER, 0x0E2},   /* mute */
    {0xA9, HID_USAGE_CONSUMER, 0x0E9},   /* volume up */
    {0xAA, HID_USAGE_CONSUMER, 0x0EA},   /* volume down */
    {0xAB, HID_USAGE_CONSUMER, 0x0B5},   /* next track */
    {0xAC, HID_USAGE_CONSUMER, 0x0B6},   /* previous track */
    {0xAD, HID_USAGE_CONSUMER, 0x0B7},   /* stop */
    {0xAE, HID_USAGE_CONSUMER, 0x0CD},   /* play/pause */
    {0xAF, HID_USAGE_CONSUMER, 0x183},   /* media select */
    {0xB0, HID_USAGE_CONSUMER, 0x0B8},   /* eject */
    {0xB1, HID_USAGE_CONSUMER, 0x18A},   /* mail */
    {0xB2, HID_USAGE_CONSUMER, 0x192},   /* calculator */
    {0xB3, HID_USAGE_CONSUMER, 0x194},   /* my computer */
    {0xB4, HID_USAGE_CONSUMER, 0x221},   /* browser search */
    {0xB5, HID_USAGE_CONSUMER, 0x223},   /* browser home */
    {0xB6, HID_USAGE_CONSUMER, 0x224},   /* browser back */
    {0xB7, HID_USAGE_CONSUMER, 0x225},   /* browser forward */
    {0xB8, HID_USAGE_CONSUMER, 0x226},   /* browser stop */
    {0xB9, HID_USAGE_CONSUMER, 0x227},   /* browser refresh */
    {0xBA, HID_USAGE_CONSUMER, 0x22A},   /* browser favorites */
    {0xBB, HID_USAGE_CONSUMER, 0x0B3},   /* fast forward */
    {0xBC, HID_USAGE_CONSUMER, 0x0B4},   /* rewind */
    {0xBD, HID_USAGE_CONSUMER, 0x06F},   /* brightness up */
    {0xBE, HID_USAGE_CONSUMER, 0x070},   /* brightness down */
};

static bool is_keyboard_usage(uint16_t qmk) {
    return (qmk >= 0x04 && qmk <= 0xA4) || (qmk >= 0xE0 && qmk <= 0xE7);
}

bool via_make_binding(const char *name, uint32_t param1, uint32_t param2,
                      struct zmk_behavior_binding *out) {
    const struct device *dev = zmk_behavior_get_binding(name);
    if (!dev) {
        return false;
    }
    *out = (struct zmk_behavior_binding){
        .behavior_dev = dev->name,
        .param1 = param1,
        .param2 = param2,
    };
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_LOCAL_IDS_IN_BINDINGS)
    out->local_id = zmk_behavior_get_local_id(dev->name);
#endif
    return true;
}

static bool named(const struct zmk_behavior_binding *binding, const char *name) {
    return binding->behavior_dev && strcmp(binding->behavior_dev, name) == 0;
}

/* VIA counts layers by position; ZMK bindings hold layer ids. */
static int layer_index_of(uint32_t layer_id) {
    for (int index = 0; index < ZMK_KEYMAP_LAYERS_LEN; index++) {
        if (zmk_keymap_layer_index_to_id(index) == layer_id) {
            return index;
        }
    }
    return -1;
}

/* An 8-bit QMK basic keycode for a usage without modifiers, or 0 if there is none. */
static uint8_t basic_from_usage(uint8_t page, uint16_t id) {
    if (page == HID_USAGE_KEY && is_keyboard_usage(id)) {
        return id;
    }
    for (size_t i = 0; i < ARRAY_SIZE(usage_aliases); i++) {
        if (usage_aliases[i].page == page && usage_aliases[i].id == id) {
            return usage_aliases[i].qmk;
        }
    }
    return 0;
}

static uint16_t keycode_from_key_press(uint32_t encoded) {
    uint8_t zmk_mods = SELECT_MODS(encoded);
    uint8_t basic = basic_from_usage(ZMK_HID_USAGE_PAGE(encoded), ZMK_HID_USAGE_ID(encoded));
    if (!basic) {
        return VIA_KC_ZMK_OTHER;
    }
    if (!zmk_mods) {
        return basic;
    }
    /* QMK can hold left-hand or right-hand modifiers, not a mix of both. */
    uint8_t left = zmk_mods & 0x0F, right = zmk_mods >> 4;
    if (left && right) {
        return VIA_KC_ZMK_OTHER;
    }
    uint8_t qmk_mods = left ? left : (right | QMK_MOD_RIGHT);
    return QK_MODS | (qmk_mods << 8) | basic;
}

uint16_t via_keycode_from_binding(const struct zmk_behavior_binding *binding) {
    if (named(binding, "none")) {
        return VIA_KC_NO;
    }
    if (named(binding, "transparent")) {
        return VIA_KC_TRANSPARENT;
    }
    if (named(binding, "key_press")) {
        return keycode_from_key_press(binding->param1);
    }
    if (named(binding, "momentary_layer") || named(binding, "toggle_layer") ||
        named(binding, "to_layer")) {
        int index = layer_index_of(binding->param1);
        if (index < 0 || index > QK_LAYER_MASK) {
            return VIA_KC_ZMK_OTHER;
        }
        uint16_t base = named(binding, "momentary_layer") ? QK_MOMENTARY
                        : named(binding, "toggle_layer")  ? QK_TOGGLE_LAYER
                                                          : QK_TO;
        return base | index;
    }
    if (named(binding, "via_macro") && binding->param1 <= QK_MACRO_MAX - QK_MACRO) {
        return QK_MACRO + binding->param1;
    }
    if (named(binding, "bootload")) {
        return QK_BOOTLOADER;
    }
    if (named(binding, "sysreset")) {
        return QK_REBOOT;
    }
    if (named(binding, "bluetooth")) {
        if (binding->param1 == BT_SEL_CMD && binding->param2 <= 2) {
            return QK_KB + VIA_CUSTOM_BT_1 + binding->param2;
        }
        if (binding->param1 == BT_CLR_CMD) {
            return QK_KB + VIA_CUSTOM_BT_CLEAR;
        }
    }
    if (named(binding, "outputs") && binding->param1 == OUT_TOG) {
        return QK_KB + VIA_CUSTOM_OUTPUT_TOGGLE;
    }
    if (named(binding, "studio_unlock")) {
        return QK_KB + VIA_CUSTOM_STUDIO_UNLOCK;
    }
    return VIA_KC_ZMK_OTHER;
}

bool via_basic_keycode_binding(uint8_t basic, uint8_t zmk_mods, struct zmk_behavior_binding *out) {
    uint32_t usage;
    if (is_keyboard_usage(basic)) {
        usage = ZMK_HID_USAGE(HID_USAGE_KEY, basic);
    } else {
        size_t i = 0;
        while (i < ARRAY_SIZE(usage_aliases) && usage_aliases[i].qmk != basic) {
            i++;
        }
        if (i == ARRAY_SIZE(usage_aliases)) {
            return false;
        }
        usage = ZMK_HID_USAGE(usage_aliases[i].page, usage_aliases[i].id);
    }
    return via_make_binding("key_press", ((uint32_t)zmk_mods << 24) | usage, 0, out);
}

bool via_keycode_to_binding(uint16_t keycode, struct zmk_behavior_binding *out) {
    if (keycode == VIA_KC_NO) {
        return via_make_binding("none", 0, 0, out);
    }
    if (keycode == VIA_KC_TRANSPARENT) {
        return via_make_binding("transparent", 0, 0, out);
    }
    if (keycode <= QK_BASIC_MAX) {
        return via_basic_keycode_binding(keycode, 0, out);
    }
    if (keycode >= QK_MODS && keycode <= QK_MODS_MAX) {
        uint8_t qmk_mods = (keycode >> 8) & 0x1F;
        uint8_t zmk_mods = (qmk_mods & QMK_MOD_RIGHT) ? (qmk_mods & 0x0F) << 4 : qmk_mods;
        return via_basic_keycode_binding(keycode & 0xFF, zmk_mods, out);
    }
    if (keycode >= QK_TO && keycode < QK_TOGGLE_LAYER + 0x20) {
        uint16_t base = keycode & ~QK_LAYER_MASK;
        uint8_t index = keycode & QK_LAYER_MASK;
        const char *name = base == QK_MOMENTARY      ? "momentary_layer"
                           : base == QK_TOGGLE_LAYER ? "toggle_layer"
                           : base == QK_TO           ? "to_layer"
                                                     : NULL;
        zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(index);
        if (!name || index >= ZMK_KEYMAP_LAYERS_LEN || id == UINT8_MAX) {
            return false;
        }
        return via_make_binding(name, id, 0, out);
    }
    if (keycode >= QK_MACRO && keycode <= QK_MACRO_MAX) {
        uint8_t index = keycode - QK_MACRO;
        return index < via_macro_count() && via_make_binding("via_macro", index, 0, out);
    }
    switch (keycode) {
    case QK_BOOTLOADER:
        return via_make_binding("bootload", 0, 0, out);
    case QK_REBOOT:
        return via_make_binding("sysreset", 0, 0, out);
    case QK_KB + VIA_CUSTOM_BT_1:
    case QK_KB + VIA_CUSTOM_BT_2:
    case QK_KB + VIA_CUSTOM_BT_3:
        return via_make_binding("bluetooth", BT_SEL_CMD, keycode - QK_KB - VIA_CUSTOM_BT_1, out);
    case QK_KB + VIA_CUSTOM_BT_CLEAR:
        return via_make_binding("bluetooth", BT_CLR_CMD, 0, out);
    case QK_KB + VIA_CUSTOM_OUTPUT_TOGGLE:
        return via_make_binding("outputs", OUT_TOG, 0, out);
    case QK_KB + VIA_CUSTOM_STUDIO_UNLOCK:
        return via_make_binding("studio_unlock", 0, 0, out);
    default:
        return false;
    }
}
