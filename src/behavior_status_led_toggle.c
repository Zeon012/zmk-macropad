/*
 * &sl_tog: turns the status LED off and on (src/macropad_status_led.c).
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_status_led_toggle

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include "macropad_status_led.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int sl_tog_pressed(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    macropad_status_led_toggle();
    return ZMK_BEHAVIOR_OPAQUE;
}

static int sl_tog_released(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api sl_tog_driver_api = {
    .binding_pressed = sl_tog_pressed,
    .binding_released = sl_tog_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &sl_tog_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
