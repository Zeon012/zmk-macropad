/*
 * Parameters for the &ir behavior (src/ir). A key bound to &ir IR_VOL_UP sends
 * whatever code the active code set stores for "volume up", so the same keymap
 * drives any TV once a different set is selected.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* Logical functions: one entry per function in every code set. */
#define IR_POWER 0
#define IR_VOL_UP 1
#define IR_VOL_DN 2
#define IR_MUTE 3
#define IR_CH_UP 4
#define IR_CH_DN 5
#define IR_N0 6
#define IR_N1 7
#define IR_N2 8
#define IR_N3 9
#define IR_N4 10
#define IR_N5 11
#define IR_N6 12
#define IR_N7 13
#define IR_N8 14
#define IR_N9 15
#define IR_UP 16
#define IR_DOWN 17
#define IR_LEFT 18
#define IR_RIGHT 19
#define IR_OK 20
#define IR_MENU 21
#define IR_BACK 22
#define IR_HOME 23
#define IR_EXIT 24
#define IR_INFO 25
#define IR_INPUT 26
#define IR_GUIDE 27
#define IR_PLAY 28
#define IR_PAUSE 29
#define IR_STOP 30
#define IR_FF 31
#define IR_REW 32
#define IR_NEXT 33
#define IR_PREV 34
#define IR_REC 35
#define IR_RED 36
#define IR_GREEN 37
#define IR_YELLOW 38
#define IR_BLUE 39
#define IR_SUBTITLE 40
#define IR_AUDIO 41
#define IR_EJECT 42
#define IR_SLEEP 43
#define IR_TEMP_UP 44
#define IR_TEMP_DN 45
#define IR_MODE 46
#define IR_FAN 47
#define IR_FN_COUNT 48

/* Code-set slot control. */
#define IR_SLOT_NEXT 100
#define IR_SLOT_0 101
#define IR_SLOT_1 102
#define IR_SLOT_2 103
#define IR_SLOT_3 104

/* Protocols, named as in Flipper-IRDB files. */
#define IR_PROTO_NONE 0
#define IR_PROTO_NEC 1      /* 8-bit address, 8-bit command */
#define IR_PROTO_NECEXT 2   /* 16-bit address, 8- or 16-bit command */
#define IR_PROTO_SAMSUNG32 3
#define IR_PROTO_SIRC 4     /* Sony 12-bit: 7-bit command, 5-bit address */
#define IR_PROTO_SIRC15 5   /* 7-bit command, 8-bit address */
#define IR_PROTO_SIRC20 6   /* 7-bit command, 13-bit address */
#define IR_PROTO_RC5 7      /* Philips, 5-bit address, 6-bit command */
#define IR_PROTO_RC5X 8     /* 7-bit command */
#define IR_PROTO_RC6 9      /* mode 0, 8-bit address, 8-bit command */
