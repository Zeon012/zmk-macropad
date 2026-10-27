/*
 * &ir FUNCTION: sends FUNCTION from the active code set (IR_POWER, IR_VOL_UP,
 * ... in dt-bindings/zmk/ir.h), or switches sets with IR_SLOT_NEXT / IR_SLOT_n.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_ir

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include "ir.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int ir_pressed(struct zmk_behavior_binding *binding,
                      struct zmk_behavior_binding_event event) {
    uint32_t p = binding->param1;
    if (p == IR_SLOT_NEXT) {
        ir_select_slot((ir_active_slot() + 1) % CONFIG_MACROPAD_IR_SLOTS);
    } else if (p >= IR_SLOT_0 && p < IR_SLOT_0 + CONFIG_MACROPAD_IR_SLOTS) {
        ir_select_slot(p - IR_SLOT_0);
    } else {
        ir_function_pressed(p);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int ir_released(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    if (binding->param1 < IR_FN_COUNT) {
        ir_function_released(binding->param1);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param_values[] = {
    {
        .display_name = "IR function",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_RANGE,
        .range = {.min = 0, .max = IR_FN_COUNT - 1},
    },
    {
        .display_name = "IR code set",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_RANGE,
        .range = {.min = IR_SLOT_NEXT, .max = IR_SLOT_0 + CONFIG_MACROPAD_IR_SLOTS - 1},
    },
};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif

static const struct behavior_driver_api ir_driver_api = {
    .binding_pressed = ir_pressed,
    .binding_released = ir_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &ir_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
