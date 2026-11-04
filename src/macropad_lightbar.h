/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

/* Switch the light bar between ZMK's RGB effects and the status display. */
int macropad_lightbar_toggle_mode(void);
bool macropad_lightbar_status_mode(void);
