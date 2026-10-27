/*
 * IR code sets: CONFIG_MACROPAD_IR_SLOTS sets of logical functions, kept in
 * settings so the configurator can replace them, and the active slot. Holding
 * a repeatable function (volume, channel, arrows, temperature) resends it.
 *
 * Built-in defaults until the configurator writes a slot:
 *   slot 0  Samsung TV (Samsung32, address 0x07)  - common codes, untested here
 *   slot 1  Shinco AC  (NECext, address 0xE710)   - captured from its remote
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "ir.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define SAMSUNG(c) {.proto = IR_PROTO_SAMSUNG32, .addr = 0x07, .cmd = (c)}
#define SHINCO(c) {.proto = IR_PROTO_NECEXT, .addr = 0xE710, .cmd = (c)}

static struct ir_set sets[CONFIG_MACROPAD_IR_SLOTS] = {
    [0] = {
        .name = "Samsung TV",
        .codes = {
            [IR_POWER] = SAMSUNG(0x02), [IR_VOL_UP] = SAMSUNG(0x07), [IR_VOL_DN] = SAMSUNG(0x0B),
            [IR_MUTE] = SAMSUNG(0x0F), [IR_CH_UP] = SAMSUNG(0x12), [IR_CH_DN] = SAMSUNG(0x10),
            [IR_N0] = SAMSUNG(0x11), [IR_N1] = SAMSUNG(0x04), [IR_N2] = SAMSUNG(0x05),
            [IR_N3] = SAMSUNG(0x06), [IR_N4] = SAMSUNG(0x08), [IR_N5] = SAMSUNG(0x09),
            [IR_N6] = SAMSUNG(0x0A), [IR_N7] = SAMSUNG(0x0C), [IR_N8] = SAMSUNG(0x0D),
            [IR_N9] = SAMSUNG(0x0E), [IR_UP] = SAMSUNG(0x60), [IR_DOWN] = SAMSUNG(0x61),
            [IR_LEFT] = SAMSUNG(0x65), [IR_RIGHT] = SAMSUNG(0x62), [IR_OK] = SAMSUNG(0x68),
            [IR_MENU] = SAMSUNG(0x1A), [IR_BACK] = SAMSUNG(0x58), [IR_HOME] = SAMSUNG(0x79),
            [IR_EXIT] = SAMSUNG(0x2D), [IR_INFO] = SAMSUNG(0x1F), [IR_INPUT] = SAMSUNG(0x01),
            [IR_GUIDE] = SAMSUNG(0x4F), [IR_PLAY] = SAMSUNG(0x47), [IR_PAUSE] = SAMSUNG(0x4A),
            [IR_STOP] = SAMSUNG(0x46), [IR_FF] = SAMSUNG(0x48), [IR_REW] = SAMSUNG(0x45),
            [IR_REC] = SAMSUNG(0x49), [IR_RED] = SAMSUNG(0x6C), [IR_GREEN] = SAMSUNG(0x14),
            [IR_YELLOW] = SAMSUNG(0x15), [IR_BLUE] = SAMSUNG(0x16), [IR_SUBTITLE] = SAMSUNG(0x25),
            [IR_SLEEP] = SAMSUNG(0x03),
        },
    },
#if CONFIG_MACROPAD_IR_SLOTS > 1
    [1] = {
        .name = "Shinco AC",
        .codes = {
            [IR_POWER] = SHINCO(0x00), [IR_TEMP_UP] = SHINCO(0x15), [IR_TEMP_DN] = SHINCO(0x0D),
            [IR_VOL_UP] = SHINCO(0x15), [IR_VOL_DN] = SHINCO(0x0D), [IR_MODE] = SHINCO(0x14),
            [IR_FAN] = SHINCO(0x16), [IR_SLEEP] = SHINCO(0x0A), [IR_INFO] = SHINCO(0x04),
        },
    },
#endif
};

static uint8_t active;

static bool repeatable(uint8_t f) {
    switch (f) {
    case IR_VOL_UP:
    case IR_VOL_DN:
    case IR_CH_UP:
    case IR_CH_DN:
    case IR_UP:
    case IR_DOWN:
    case IR_LEFT:
    case IR_RIGHT:
    case IR_TEMP_UP:
    case IR_TEMP_DN:
        return true;
    default:
        return false;
    }
}

static uint8_t held = 0xff;
static void repeat_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(repeat_work, repeat_handler);

static void repeat_handler(struct k_work *work) {
    if (held >= IR_FN_COUNT) {
        return;
    }
    ir_tx_send(&sets[active].codes[held], true);
    k_work_reschedule(&repeat_work, K_MSEC(CONFIG_MACROPAD_IR_REPEAT_MS));
}

void ir_function_pressed(uint8_t f) {
    if (f >= IR_FN_COUNT) {
        return;
    }
    ir_tx_send(&sets[active].codes[f], false);
    if (repeatable(f)) {
        held = f;
        k_work_reschedule(&repeat_work, K_MSEC(CONFIG_MACROPAD_IR_REPEAT_DELAY_MS));
    }
}

void ir_function_released(uint8_t f) {
    if (f == held) {
        held = 0xff;
        k_work_cancel_delayable(&repeat_work);
    }
}

int ir_active_slot(void) { return active; }

void ir_select_slot(int slot) {
    if (slot < 0 || slot >= CONFIG_MACROPAD_IR_SLOTS || slot == active) {
        return;
    }
    active = slot;
    settings_save_one("ir/active", &active, sizeof(active));
    LOG_INF("IR code set %d: %s", slot, sets[slot].name);
}

const struct ir_set *ir_set_get(int slot) {
    return (slot >= 0 && slot < CONFIG_MACROPAD_IR_SLOTS) ? &sets[slot] : NULL;
}

int ir_set_put(int slot, const struct ir_set *set) {
    if (slot < 0 || slot >= CONFIG_MACROPAD_IR_SLOTS) {
        return -EINVAL;
    }
    sets[slot] = *set;
    sets[slot].name[IR_SET_NAME_LEN - 1] = '\0';
    char key[] = "ir/set/0";
    key[7] = '0' + slot;
    return settings_save_one(key, &sets[slot], sizeof(sets[slot]));
}

static int ir_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    if (settings_name_steq(name, "active", &next) && !next) {
        uint8_t v;
        if (len == sizeof(v) && read_cb(cb_arg, &v, sizeof(v)) == sizeof(v) &&
            v < CONFIG_MACROPAD_IR_SLOTS) {
            active = v;
        }
        return 0;
    }
    if (settings_name_steq(name, "set", &next) && next && next[0] >= '0' && !next[1]) {
        int slot = next[0] - '0';
        if (slot < CONFIG_MACROPAD_IR_SLOTS && len == sizeof(sets[slot])) {
            read_cb(cb_arg, &sets[slot], sizeof(sets[slot]));
            sets[slot].name[IR_SET_NAME_LEN - 1] = '\0';
        }
        return 0;
    }
    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(macropad_ir, "ir", NULL, ir_settings_set, NULL, NULL);
