/*
 * Light bar status mode. The underglow strip (zmk,underglow) runs ZMK's RGB
 * effects as usual; &lb_mode switches it to a status display and back:
 *
 *   LED 1  output        green = USB, profile colour = Bluetooth
 *   LED 2  Bluetooth     selected profile's colour (1 blue, 2 cyan, 3 magenta):
 *                        solid = connected, fast blink = pairing,
 *                        short flash every 2 s = paired but not connected
 *   LED 3  battery       green >= 50 %, yellow >= 20 %, red below, blinking <= 15 %
 *   LED 4  layer         off on the base layer, purple = layer 1, orange = 2, white = 3+
 *
 * In status mode underglow is switched off (its on/off state is remembered and
 * restored on the way back) and this module writes the strip itself. On
 * battery the bar goes dark while the pad is idle. The mode survives restarts.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include <drivers/ext_power.h>
#include <zmk/activity.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
#include <zmk/battery.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#endif

#include "macropad_lightbar.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#define STRIP_LEN DT_PROP(STRIP_NODE, chain_length)
#define TICK_MS 125
#define SEARCHING_PERIOD_TICKS (2000 / TICK_MS)
#define LOW_BATTERY_BLINK_TICKS (500 / TICK_MS)
#define LOW_BATTERY_PERCENT 15
#define START_DELAY_MS 1000 /* let underglow finish starting before taking over */

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER) && DT_HAS_COMPAT_STATUS_OKAY(zmk_ext_power_generic)
static const struct device *const ext_power = DEVICE_DT_GET(DT_INST(0, zmk_ext_power_generic));
#else
static const struct device *const ext_power = NULL;
#endif

/* Saved together under "mp_lightbar/state". */
static struct {
    bool status_mode;
    bool underglow_was_on;
} state;

static uint32_t tick;
static struct led_rgb pixels[STRIP_LEN];

static void lightbar_tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(lightbar_work, lightbar_tick);

struct rgb8 {
    uint8_t r, g, b;
};

static const struct rgb8 BLACK = {0, 0, 0}, RED = {255, 0, 0}, GREEN = {0, 255, 0},
                         BLUE = {0, 0, 255}, CYAN = {0, 255, 255}, MAGENTA = {255, 0, 255},
                         YELLOW = {255, 160, 0}, ORANGE = {255, 80, 0}, PURPLE = {140, 0, 255},
                         WHITE = {255, 255, 255};

static const struct rgb8 *const profile_colors[] = {&BLUE, &CYAN, &MAGENTA};

static void set_pixel(int index, struct rgb8 c) {
    if (index >= STRIP_LEN) {
        return;
    }
    pixels[index] = (struct led_rgb){
        .r = c.r * CONFIG_MACROPAD_LIGHTBAR_BRIGHTNESS / 100,
        .g = c.g * CONFIG_MACROPAD_LIGHTBAR_BRIGHTNESS / 100,
        .b = c.b * CONFIG_MACROPAD_LIGHTBAR_BRIGHTNESS / 100,
    };
}

static bool usb_powered(void) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    return zmk_usb_is_powered();
#else
    return false;
#endif
}

static int battery_percent(void) {
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    return zmk_battery_state_of_charge();
#else
    return 100;
#endif
}

static void ensure_strip_power(void) {
    if (ext_power && device_is_ready(ext_power) && ext_power_get(ext_power) <= 0) {
        ext_power_enable(ext_power);
    }
}

static void render_status(void) {
    int profile = zmk_ble_active_profile_index();
    struct rgb8 profile_color =
        (profile >= 0 && profile < ARRAY_SIZE(profile_colors)) ? *profile_colors[profile] : WHITE;

    /* LED 1: output */
    bool usb_out = zmk_endpoint_get_selected().transport == ZMK_TRANSPORT_USB;
    set_pixel(0, usb_out ? GREEN : profile_color);

    /* LED 2: Bluetooth */
    struct rgb8 bt = profile_color;
    if (!zmk_ble_active_profile_is_connected()) {
        if (zmk_ble_active_profile_is_open()) {
            bt = tick % 2 == 0 ? profile_color : BLACK; /* pairing */
        } else {
            bt = tick % SEARCHING_PERIOD_TICKS == 0 ? profile_color : BLACK;
        }
    }
    set_pixel(1, bt);

    /* LED 3: battery */
    int pct = battery_percent();
    struct rgb8 batt = pct >= 50 ? GREEN : pct >= 20 ? YELLOW : RED;
    if (pct <= LOW_BATTERY_PERCENT && (tick / LOW_BATTERY_BLINK_TICKS) % 2) {
        batt = BLACK;
    }
    set_pixel(2, batt);

    /* LED 4: layer */
    zmk_keymap_layer_index_t layer = zmk_keymap_highest_layer_active();
    set_pixel(3, layer == 0 ? BLACK : layer == 1 ? PURPLE : layer == 2 ? ORANGE : WHITE);
}

static void lightbar_tick(struct k_work *work) {
    if (!state.status_mode) {
        return;
    }
    tick++;
    memset(pixels, 0, sizeof(pixels));

    bool lit = usb_powered() || zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE;
    if (lit) {
        ensure_strip_power();
        render_status();
    }
    /* Rewritten every tick: the strip forgets its colours when its power is cut. */
    led_strip_update_rgb(strip, pixels, STRIP_LEN);
    k_work_reschedule(&lightbar_work, K_MSEC(TICK_MS));
}

static void save_state(void) {
#if IS_ENABLED(CONFIG_SETTINGS)
    settings_save_one("mp_lightbar/state", &state, sizeof(state));
#endif
}

static void enter_status_mode(void) {
    bool on = false;
    zmk_rgb_underglow_get_state(&on);
    if (on) {
        state.underglow_was_on = true;
        zmk_rgb_underglow_off(); /* also cuts strip power; the tick turns it back on */
    }
    k_work_reschedule(&lightbar_work, K_MSEC(50));
}

int macropad_lightbar_toggle_mode(void) {
    state.status_mode = !state.status_mode;
    if (state.status_mode) {
        state.underglow_was_on = false;
        enter_status_mode();
    } else {
        k_work_cancel_delayable(&lightbar_work);
        memset(pixels, 0, sizeof(pixels));
        led_strip_update_rgb(strip, pixels, STRIP_LEN);
        if (state.underglow_was_on) {
            zmk_rgb_underglow_on();
        }
    }
    save_state();
    return 0;
}

bool macropad_lightbar_status_mode(void) { return state.status_mode; }

#if IS_ENABLED(CONFIG_SETTINGS)
static int lightbar_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                 void *cb_arg) {
    if (settings_name_steq(name, "state", NULL) && len == sizeof(state)) {
        return read_cb(cb_arg, &state, sizeof(state)) == sizeof(state) ? 0 : -EINVAL;
    }
    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(macropad_lightbar, "mp_lightbar", NULL, lightbar_settings_set, NULL,
                               NULL);
#endif

static void lightbar_start(struct k_work *work) {
    if (state.status_mode) {
        bool was = state.underglow_was_on;
        enter_status_mode();
        state.underglow_was_on = state.underglow_was_on || was;
    }
}

static K_WORK_DELAYABLE_DEFINE(lightbar_start_work, lightbar_start);

static int lightbar_init(void) {
    if (!device_is_ready(strip)) {
        LOG_ERR("Light bar strip is not ready");
        return -ENODEV;
    }
    k_work_reschedule(&lightbar_start_work, K_MSEC(START_DELAY_MS));
    return 0;
}

SYS_INIT(lightbar_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
