/*
 * TV remote slide switch (SW1, 3 positions, common to GND): TV closes
 * mode-tv-gpios, VIDEO closes mode-video-gpios, DVD closes neither.
 *
 *   TV     only the base layer (infrared, active code set)
 *   VIDEO  layer TVREMOTE_LAYER_VIDEO on (Bluetooth keyboard)
 *   DVD    layer TVREMOTE_LAYER_DVD on, infrared locked to code set 0
 *
 * The switch is polled rather than wired to interrupts: the pull-ups are on
 * for a few microseconds per read and the pins are disconnected between reads,
 * so a switch resting in TV or VIDEO doesn't drain the AAs through a pull-up,
 * and it can never wake the remote from deep sleep.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/keymap.h>

#if IS_ENABLED(CONFIG_MACROPAD_IR)
#include "ir/ir.h"
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec sw_tv = GPIO_DT_SPEC_GET(USER_NODE, mode_tv_gpios);
static const struct gpio_dt_spec sw_video = GPIO_DT_SPEC_GET(USER_NODE, mode_video_gpios);

enum mode { MODE_TV, MODE_VIDEO, MODE_DVD, MODE_NONE };
static const char *const mode_names[] = {"TV", "VIDEO", "DVD"};

static enum mode current = MODE_NONE;
static enum mode candidate = MODE_NONE;

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

static enum mode read_mode(void) {
    int tv = read_closed(&sw_tv);
    int video = read_closed(&sw_video);
    if (tv < 0 || video < 0) {
        return MODE_NONE;
    }
    if (tv && !video) {
        return MODE_TV;
    }
    if (video && !tv) {
        return MODE_VIDEO;
    }
    /* Neither closed is DVD. Both closed can't happen on a PCM13; treat it
     * as mid-travel and keep the last mode. */
    return tv ? MODE_NONE : MODE_DVD;
}

static void set_layer(int index, bool on) {
    zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(index);
    if (on) {
        zmk_keymap_layer_activate(id, false);
    } else {
        zmk_keymap_layer_deactivate(id, false);
    }
}

static void apply(enum mode m) {
    set_layer(CONFIG_TVREMOTE_LAYER_VIDEO, m == MODE_VIDEO);
    set_layer(CONFIG_TVREMOTE_LAYER_DVD, m == MODE_DVD);
#if IS_ENABLED(CONFIG_MACROPAD_IR)
    ir_lock_slot(m == MODE_DVD ? 0 : -1);
#endif
    LOG_INF("Slide switch: %s", mode_names[m]);
}

static void poll_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(poll_work, poll_handler);

static void poll_handler(struct k_work *work) {
    enum mode m = read_mode();
    /* Two matching reads in a row before acting, to ride out contact bounce
     * and the gap between positions. */
    if (m != MODE_NONE && m == candidate && m != current) {
        current = m;
        apply(m);
    }
    candidate = m;
    k_work_reschedule(&poll_work, K_MSEC(CONFIG_TVREMOTE_MODE_POLL_MS));
}

static int tvremote_mode_init(void) {
    if (!gpio_is_ready_dt(&sw_tv) || !gpio_is_ready_dt(&sw_video)) {
        LOG_ERR("Slide switch GPIOs not ready");
        return -ENODEV;
    }
    k_work_reschedule(&poll_work, K_MSEC(50));
    return 0;
}

SYS_INIT(tvremote_mode_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
