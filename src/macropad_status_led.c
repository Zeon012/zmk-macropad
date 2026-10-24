/*
 * Power and Bluetooth status on an RGB LED, dimmed to
 * CONFIG_MACROPAD_STATUS_LED_BRIGHTNESS percent. The LED is either three PWM
 * channels (status_led_red, status_led_green, status_led_blue) or, if the
 * board has a node labelled status_pixel, the first pixel of that LED strip
 * (an addressable WS2812).
 *
 * Power source, shown whenever nothing else is:
 *   green             running on USB power
 *   red               running on the battery (off while the pad is idle)
 *   red, slow blink   battery low
 *
 * Bluetooth, in the selected profile's colour (profile 1 blue, 2 cyan,
 * 3 magenta, others white):
 *   fast blink        the profile has no host yet: pairing mode
 *   short flash / 2 s paired, but the host is not connected
 *   solid for 1.5 s   just connected
 *
 * On USB power the Bluetooth patterns only show for a minute after start-up
 * or after a profile is selected, cleared, connected or disconnected, so a
 * pad used over USB is not left blinking.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/activity.h>
#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
#include <zmk/battery.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define TICK_MS 125
#define CONNECTED_SOLID_TICKS (1500 / TICK_MS)
#define SEARCHING_PERIOD_TICKS (2000 / TICK_MS)
#define LOW_BATTERY_BLINK_TICKS (500 / TICK_MS)
#define LOW_BATTERY_PERCENT 15
#define BLUETOOTH_WINDOW_TICKS (60000 / TICK_MS)

#define RED BIT(0)
#define GREEN BIT(1)
#define BLUE BIT(2)
#define OFF 0

#define STATUS_PIXEL DT_NODELABEL(status_pixel)
#define USE_PIXEL DT_NODE_HAS_STATUS(STATUS_PIXEL, okay)

#if USE_PIXEL
static const struct device *const pixel = DEVICE_DT_GET(STATUS_PIXEL);
#else
/* Order: red, green, blue. */
static const struct pwm_dt_spec leds[] = {
    PWM_DT_SPEC_GET(DT_NODELABEL(status_led_red)),
    PWM_DT_SPEC_GET(DT_NODELABEL(status_led_green)),
    PWM_DT_SPEC_GET(DT_NODELABEL(status_led_blue)),
};
#endif

static const uint8_t profile_colors[] = {BLUE, GREEN | BLUE, RED | BLUE};

static uint32_t tick;
static bool was_connected;
static uint32_t connected_ticks;
static uint32_t bluetooth_event_tick;

static void status_led_tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(status_led_work, status_led_tick);

static void set_color(uint8_t color) {
#if USE_PIXEL
    uint8_t level = 255 * CONFIG_MACROPAD_STATUS_LED_BRIGHTNESS / 100;
    struct led_rgb rgb = {
        .r = (color & RED) ? level : 0,
        .g = (color & GREEN) ? level : 0,
        .b = (color & BLUE) ? level : 0,
    };
    /* Rewritten every tick: the pixel forgets its colour when the board cuts
     * its power, so sending only changes could leave it dark. */
    led_strip_update_rgb(pixel, &rgb, 1);
#else
    for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
        uint32_t pulse =
            (color & BIT(i)) ? (leds[i].period / 100) * CONFIG_MACROPAD_STATUS_LED_BRIGHTNESS : 0;
        pwm_set_pulse_dt(&leds[i], pulse);
    }
#endif
}

static bool usb_powered(void) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    return zmk_usb_is_powered();
#else
    return false;
#endif
}

static bool battery_is_low(void) {
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    return zmk_battery_state_of_charge() <= LOW_BATTERY_PERCENT;
#else
    return false;
#endif
}

static void status_led_tick(struct k_work *work) {
    int profile = zmk_ble_active_profile_index();
    uint8_t profile_color = (profile >= 0 && profile < ARRAY_SIZE(profile_colors))
                                ? profile_colors[profile]
                                : (RED | GREEN | BLUE);
    bool on_usb = usb_powered();
    /* USB power is free, so the LED may stay lit while the pad is idle. */
    bool lit = on_usb || zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE;
    uint8_t color = OFF;
    tick++;

    if (lit) {
        if (on_usb) {
            color = GREEN;
        } else if (!battery_is_low() || (tick / LOW_BATTERY_BLINK_TICKS) % 2 == 0) {
            color = RED;
        }
    }

    bool connected = zmk_ble_active_profile_is_connected();
    if (connected && !was_connected) {
        connected_ticks = 0;
    }
    was_connected = connected;

    bool show_bluetooth = lit && (!on_usb || tick - bluetooth_event_tick < BLUETOOTH_WINDOW_TICKS);
    if (show_bluetooth) {
        if (connected) {
            if (connected_ticks < CONNECTED_SOLID_TICKS) {
                connected_ticks++;
                color = profile_color;
            }
        } else if (zmk_ble_active_profile_is_open()) {
            color = tick % 2 == 0 ? profile_color : OFF;
        } else if (tick % SEARCHING_PERIOD_TICKS == 0) {
            color = profile_color;
        }
    }

    set_color(color);
    k_work_reschedule(&status_led_work, K_MSEC(TICK_MS));
}

static int status_led_listener(const zmk_event_t *eh) {
    bluetooth_event_tick = tick;
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(macropad_status_led, status_led_listener);
ZMK_SUBSCRIPTION(macropad_status_led, zmk_ble_active_profile_changed);

static int status_led_init(void) {
#if USE_PIXEL
    if (!device_is_ready(pixel)) {
        LOG_ERR("Status LED pixel is not ready");
        return -ENODEV;
    }
    set_color(OFF);
#else
    for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
        if (!pwm_is_ready_dt(&leds[i])) {
            LOG_ERR("Status LED pin %d is not ready", (int)i);
            return -ENODEV;
        }
        pwm_set_pulse_dt(&leds[i], 0);
    }
#endif

    k_work_reschedule(&status_led_work, K_MSEC(TICK_MS));
    return 0;
}

SYS_INIT(status_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
