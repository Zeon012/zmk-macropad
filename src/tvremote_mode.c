/*
 * TV remote mode, as on the original Sony: a 2-position slide switch picks
 * TV or DVD/VIDEO, and the SELECT DVD / SELECT VIDEO buttons (&tvsel) pick
 * which device the DVD/VIDEO position controls.
 *
 *   switch TV                  base layer, infrared code set 0
 *   switch DVD/VIDEO + DVD     layer TVREMOTE_LAYER_DVD, infrared code set 1
 *   switch DVD/VIDEO + VIDEO   layer TVREMOTE_LAYER_VIDEO (Bluetooth keyboard)
 *
 * The PCB has a 3-position PCM13 (common to GND; TV closes mode-tv-gpios, the
 * middle stop closes nothing, the far stop closes mode-video-gpios). The
 * shell's window only lets the cap reach TV and the middle stop; the far stop
 * counts as DVD/VIDEO too.
 *
 * The switch is polled rather than wired to interrupts: the pull-up is on for
 * a few microseconds per read and the pin is disconnected between reads, so a
 * switch resting on TV doesn't drain the AAs through a pull-up, and it can
 * never wake the remote from deep sleep. The SELECT choice is saved, so it
 * survives sleep and battery changes.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_tvremote_select

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>

#include <dt-bindings/zmk/tvremote.h>

#if IS_ENABLED(CONFIG_MACROPAD_IR)
#include "ir/ir.h"
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec sw_tv = GPIO_DT_SPEC_GET(USER_NODE, mode_tv_gpios);

enum mode { MODE_TV, MODE_DVD, MODE_VIDEO, MODE_NONE };
static const char *const mode_names[] = {"TV", "DVD", "VIDEO"};

static int8_t switch_tv = -1;        /* debounced switch state, -1 = not read yet */
static int8_t candidate = -1;        /* previous raw read, for debouncing */
static uint8_t selected = TVSEL_DVD; /* SELECT choice for the DVD/VIDEO position */
static enum mode current = MODE_NONE;

static int read_closed(const struct gpio_dt_spec *sw) {
    /* Devicetree flags give active-low and the pull-up. */
    if (gpio_pin_configure_dt(sw, GPIO_INPUT)) {
        return -1;
    }
    k_busy_wait(20);
    int closed = gpio_pin_get_dt(sw);
    gpio_pin_configure(sw->port, sw->pin, GPIO_DISCONNECTED);
    return closed;
}

static void set_layer(int index, bool on) {
    zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(index);
    if (on) {
        zmk_keymap_layer_activate(id, false);
    } else {
        zmk_keymap_layer_deactivate(id, false);
    }
}

static void update(void) {
    if (switch_tv < 0) {
        return;
    }
    enum mode m = switch_tv ? MODE_TV : (selected == TVSEL_VIDEO ? MODE_VIDEO : MODE_DVD);
    if (m == current) {
        return;
    }
    current = m;
    set_layer(CONFIG_TVREMOTE_LAYER_VIDEO, m == MODE_VIDEO);
    set_layer(CONFIG_TVREMOTE_LAYER_DVD, m == MODE_DVD);
#if IS_ENABLED(CONFIG_MACROPAD_IR)
    ir_lock_slot(m == MODE_TV ? 0 : m == MODE_DVD ? 1 : -1);
#endif
    LOG_INF("Remote mode: %s", mode_names[m]);
}

static void poll_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(poll_work, poll_handler);

static void poll_handler(struct k_work *work) {
    int tv = read_closed(&sw_tv);
    /* Two matching reads in a row before acting, to ride out contact bounce. */
    if (tv >= 0 && tv == candidate && tv != switch_tv) {
        switch_tv = tv;
        update();
    }
    candidate = tv;
    k_work_reschedule(&poll_work, K_MSEC(CONFIG_TVREMOTE_MODE_POLL_MS));
}

static int tvremote_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                 void *cb_arg) {
    uint8_t value;
    if (!strcmp(name, "select") && len == sizeof(value) &&
        read_cb(cb_arg, &value, sizeof(value)) == sizeof(value) && value <= TVSEL_VIDEO) {
        selected = value;
        update(); /* in case the switch was read before settings loaded */
    }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(tvremote, "tvremote", NULL, tvremote_settings_set, NULL, NULL);

static int tvremote_mode_init(void) {
    if (!gpio_is_ready_dt(&sw_tv)) {
        LOG_ERR("Slide switch GPIO not ready");
        return -ENODEV;
    }
    k_work_reschedule(&poll_work, K_MSEC(50));
    return 0;
}

SYS_INIT(tvremote_mode_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/* &tvsel TVSEL_DVD / TVSEL_VIDEO: the SELECT buttons. They work in either
 * switch position; in TV the choice waits until the switch moves. */

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int tvsel_pressed(struct zmk_behavior_binding *binding,
                         struct zmk_behavior_binding_event event) {
    uint8_t value = binding->param1 == TVSEL_VIDEO ? TVSEL_VIDEO : TVSEL_DVD;
    if (value != selected) {
        selected = value;
        settings_save_one("tvremote/select", &selected, sizeof(selected));
        update();
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int tvsel_released(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param_values[] = {
    {.display_name = "DVD (infrared)", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
     .value = TVSEL_DVD},
    {.display_name = "VIDEO (Bluetooth)", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
     .value = TVSEL_VIDEO},
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

static const struct behavior_driver_api tvsel_driver_api = {
    .binding_pressed = tvsel_pressed,
    .binding_released = tvsel_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &tvsel_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
