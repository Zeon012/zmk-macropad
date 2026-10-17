/*
 * VIA compatibility: lets the VIA app (usevia.app) read and remap the keymap
 * over raw HID, and store VIA macros. VIA speaks QMK's protocol and QMK's
 * 16-bit keycodes; these files translate both to ZMK.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zmk/behavior.h>

/* Every VIA request and reply is one report of this many bytes. */
#define VIA_REPORT_SIZE 32

/* Hands a request from the host to the protocol handler; safe from USB callbacks. */
void via_receive_report(const uint8_t *data, size_t length);

/* Sends a reply to the host (via_usb_hid.c). */
void via_send_report(const uint8_t *report);

/* QMK keycodes VIA uses that need names here. */
#define VIA_KC_NO 0x0000
#define VIA_KC_TRANSPARENT 0x0001
/* Shown in VIA for a ZMK binding it has no keycode for; setting it changes nothing. */
#define VIA_KC_ZMK_OTHER (0x7E00 + 6)

/*
 * Translates a ZMK binding to the QMK keycode VIA shows. Anything without a
 * QMK equivalent comes back as VIA_KC_ZMK_OTHER.
 */
uint16_t via_keycode_from_binding(const struct zmk_behavior_binding *binding);

/*
 * Translates a QMK keycode from VIA to a ZMK binding. Returns false when the
 * keycode has no ZMK equivalent (or is VIA_KC_ZMK_OTHER); the key is then left
 * as it was.
 */
bool via_keycode_to_binding(uint16_t keycode, struct zmk_behavior_binding *binding);

/*
 * Makes a &kp binding for an 8-bit QMK basic keycode (as used inside VIA
 * macros) with the given ZMK modifier bits. False if the keycode is not a key.
 */
bool via_basic_keycode_binding(uint8_t basic, uint8_t zmk_mods, struct zmk_behavior_binding *out);

/* Makes a binding to the behavior with device name `name`, false if it is not built in. */
bool via_make_binding(const char *name, uint32_t param1, uint32_t param2,
                      struct zmk_behavior_binding *out);

/* Number of VIA macros, and the size of the buffer that holds them all. */
uint8_t via_macro_count(void);
uint16_t via_macro_buffer_size(void);

/* Reads or writes part of the macro buffer; writes are saved to flash. */
void via_macro_get_buffer(uint16_t offset, uint8_t size, uint8_t *out);
void via_macro_set_buffer(uint16_t offset, uint8_t size, const uint8_t *data);
void via_macro_reset(void);

/* Types macro `index` from the buffer, as the &via_macro behavior does. */
void via_macro_play(uint8_t index, const struct zmk_behavior_binding_event *event);
