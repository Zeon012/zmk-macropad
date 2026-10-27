/*
 * Infrared transmitter: code sets of logical functions, sent through the nRF
 * PWM so the 36-40 kHz carrier and the protocol timing come from hardware.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

#include <dt-bindings/zmk/ir.h>

#define IR_SET_NAME_LEN 24

struct ir_code {
    uint8_t proto;
    uint32_t addr;
    uint32_t cmd;
} __packed;

struct ir_set {
    char name[IR_SET_NAME_LEN];
    struct ir_code codes[IR_FN_COUNT];
} __packed;

/* ir_tx.c: queue one frame (or the protocol's usual burst of frames). A repeat
 * keeps the RC5/RC6 toggle bit, so the device treats it as the same press. */
int ir_tx_send(const struct ir_code *code, bool repeat);

/* ir_codes.c */
int ir_active_slot(void);
void ir_select_slot(int slot);
const struct ir_set *ir_set_get(int slot);
int ir_set_put(int slot, const struct ir_set *set);
void ir_function_pressed(uint8_t function);
void ir_function_released(uint8_t function);
