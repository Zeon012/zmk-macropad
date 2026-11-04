/*
 * &lb_mode: switches the light bar between RGB effects and the status display
 * (src/macropad_lightbar.c).
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_lightbar_mode

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include "macropad_lightbar.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int lb_mode_pressed(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    macropad_lightbar_toggle_mode();
    return ZMK_BEHAVIOR_OPAQUE;
}

static int lb_mode_released(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api lb_mode_driver_api = {
    .binding_pressed = lb_mode_pressed,
    .binding_released = lb_mode_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &lb_mode_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
