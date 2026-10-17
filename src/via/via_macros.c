/*
 * VIA macros: one buffer holding every macro back to back, each ending in a
 * zero byte, in QMK's send_string format. Plain bytes are typed as US-layout
 * characters; byte 1 starts a command: 1 1 <key> taps a key, 1 2 <key>
 * presses it, 1 3 <key> releases it, 1 4 <digits> | waits that many ms.
 * Keys are 8-bit QMK basic keycodes.
 *
 * The buffer lives in RAM and is saved to flash a moment after VIA stops
 * writing to it. Playback goes through ZMK's behavior queue, like ZMK macros.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <dt-bindings/zmk/modifiers.h>
#include <zmk/behavior_queue.h>

#include "via.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define MACRO_COMMAND 1
#define MACRO_TAP 1
#define MACRO_DOWN 2
#define MACRO_UP 3
#define MACRO_DELAY 4

#define SAVE_DELAY_MS 1000
#define SHIFT_BIT 0x80

static uint8_t buffer[CONFIG_MACROPAD_VIA_MACRO_BUFFER_SIZE];

/* QMK keycode for each ASCII character on a US layout; SHIFT_BIT marks shifted ones. */
static const uint8_t ascii_keys[128] = {
    ['\b'] = 0x2A, ['\t'] = 0x2B, ['\n'] = 0x28, [0x1B] = 0x29, [0x7F] = 0x4C,
    [' '] = 0x2C,  ['!'] = 0x1E | SHIFT_BIT, ['"'] = 0x34 | SHIFT_BIT, ['#'] = 0x20 | SHIFT_BIT,
    ['$'] = 0x21 | SHIFT_BIT, ['%'] = 0x22 | SHIFT_BIT, ['&'] = 0x24 | SHIFT_BIT, ['\''] = 0x34,
    ['('] = 0x26 | SHIFT_BIT, [')'] = 0x27 | SHIFT_BIT, ['*'] = 0x25 | SHIFT_BIT,
    ['+'] = 0x2E | SHIFT_BIT, [','] = 0x36, ['-'] = 0x2D, ['.'] = 0x37, ['/'] = 0x38,
    ['0'] = 0x27, ['1'] = 0x1E, ['2'] = 0x1F, ['3'] = 0x20, ['4'] = 0x21, ['5'] = 0x22,
    ['6'] = 0x23, ['7'] = 0x24, ['8'] = 0x25, ['9'] = 0x26,
    [':'] = 0x33 | SHIFT_BIT, [';'] = 0x33, ['<'] = 0x36 | SHIFT_BIT, ['='] = 0x2E,
    ['>'] = 0x37 | SHIFT_BIT, ['?'] = 0x38 | SHIFT_BIT, ['@'] = 0x1F | SHIFT_BIT,
    ['['] = 0x2F, ['\\'] = 0x31, [']'] = 0x30, ['^'] = 0x23 | SHIFT_BIT, ['_'] = 0x2D | SHIFT_BIT,
    ['`'] = 0x35, ['{'] = 0x2F | SHIFT_BIT, ['|'] = 0x31 | SHIFT_BIT, ['}'] = 0x30 | SHIFT_BIT,
    ['~'] = 0x35 | SHIFT_BIT,
};

static uint8_t ascii_key(uint8_t c) {
    if (c >= 'a' && c <= 'z') {
        return 0x04 + c - 'a';
    }
    if (c >= 'A' && c <= 'Z') {
        return (0x04 + c - 'A') | SHIFT_BIT;
    }
    return c < 128 ? ascii_keys[c] : 0;
}

static void save_work_handler(struct k_work *work) {
    int err = settings_save_one("via/macros", buffer, sizeof(buffer));
    if (err) {
        LOG_ERR("Saving VIA macros failed (%d)", err);
    }
}

static K_WORK_DELAYABLE_DEFINE(save_work, save_work_handler);

static int settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    if (strcmp(name, "macros") != 0) {
        return -ENOENT;
    }
    memset(buffer, 0, sizeof(buffer));
    int read = read_cb(cb_arg, buffer, MIN(len, sizeof(buffer)));
    return read < 0 ? read : 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(via, "via", NULL, settings_set, NULL, NULL);

uint8_t via_macro_count(void) { return CONFIG_MACROPAD_VIA_MACRO_COUNT; }

uint16_t via_macro_buffer_size(void) { return sizeof(buffer); }

void via_macro_get_buffer(uint16_t offset, uint8_t size, uint8_t *out) {
    for (uint8_t i = 0; i < size; i++) {
        out[i] = offset + i < sizeof(buffer) ? buffer[offset + i] : 0;
    }
}

void via_macro_set_buffer(uint16_t offset, uint8_t size, const uint8_t *data) {
    if (offset >= sizeof(buffer)) {
        return;
    }
    memcpy(&buffer[offset], data, MIN(size, sizeof(buffer) - offset));
    k_work_reschedule(&save_work, K_MSEC(SAVE_DELAY_MS));
}

void via_macro_reset(void) {
    memset(buffer, 0, sizeof(buffer));
    k_work_reschedule(&save_work, K_MSEC(SAVE_DELAY_MS));
}

static bool queue_key(const struct zmk_behavior_binding_event *event, uint8_t key, uint8_t mods,
                      bool press, bool release) {
    struct zmk_behavior_binding binding;
    if (!via_basic_keycode_binding(key, mods, &binding)) {
        return true; /* not a key; skip it */
    }
    uint32_t wait = CONFIG_MACROPAD_VIA_MACRO_TAP_MS;
    if (press && zmk_behavior_queue_add(event, binding, true, wait) < 0) {
        return false;
    }
    return !release || zmk_behavior_queue_add(event, binding, false, wait) >= 0;
}

static bool queue_delay(const struct zmk_behavior_binding_event *event, uint32_t ms) {
    struct zmk_behavior_binding none;
    return !via_make_binding("none", 0, 0, &none) ||
           zmk_behavior_queue_add(event, none, false, ms) >= 0;
}

void via_macro_play(uint8_t index, const struct zmk_behavior_binding_event *event) {
    /* QMK's rule: a non-zero last byte means a write was cut off, so play nothing. */
    if (index >= via_macro_count() || buffer[sizeof(buffer) - 1] != 0) {
        return;
    }
    size_t at = 0;
    for (uint8_t skipped = 0; skipped < index; at++) {
        if (at == sizeof(buffer)) {
            return;
        }
        if (buffer[at] == 0) {
            skipped++;
        }
    }

    bool queued = true;
    while (queued && at < sizeof(buffer) && buffer[at] != 0) {
        uint8_t c = buffer[at++];
        if (c != MACRO_COMMAND) {
            uint8_t key = ascii_key(c);
            if (key) {
                queued = queue_key(event, key & ~SHIFT_BIT, (key & SHIFT_BIT) ? MOD_LSFT : 0, true,
                                   true);
            }
            continue;
        }
        if (at + 1 >= sizeof(buffer)) {
            break;
        }
        uint8_t command = buffer[at++];
        if (command == MACRO_DELAY) {
            uint32_t ms = 0;
            while (at < sizeof(buffer) && buffer[at] >= '0' && buffer[at] <= '9') {
                ms = ms * 10 + (buffer[at++] - '0');
            }
            if (at < sizeof(buffer) && buffer[at] == '|') {
                at++;
            }
            queued = queue_delay(event, ms);
            continue;
        }
        uint8_t key = buffer[at++];
        queued = queue_key(event, key, 0, command == MACRO_TAP || command == MACRO_DOWN,
                           command == MACRO_TAP || command == MACRO_UP);
    }
    if (!queued) {
        LOG_WRN("VIA macro %d is longer than the behavior queue; the rest was dropped", index);
    }
}
