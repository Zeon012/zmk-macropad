/*
 * Bluetooth and battery status on an RGB LED (three GPIOs: red, green, blue).
 * The colour tells which Bluetooth profile is selected:
 * profile 1 blue, 2 cyan, 3 magenta, others white.
 *
 *   fast blink        the selected profile has no host yet: pairing mode
 *   short flash / 2 s paired, but the host is not connected
 *   solid for 1.5 s   just connected, then off
 *   red flash / 4 s   battery low, while running on the battery
 *
 * The LED stays off while the pad is idle, to save the battery.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/activity.h>
#include <zmk/ble.h>

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
#define LOW_BATTERY_PERIOD_TICKS (4000 / TICK_MS)
#define LOW_BATTERY_PERCENT 15

#define RED BIT(0)
#define GREEN BIT(1)
#define BLUE BIT(2)
#define OFF 0

#define LED_NODE DT_PATH(zephyr_user)

/* Order: red, green, blue. */
static const struct gpio_dt_spec leds[] = {
    GPIO_DT_SPEC_GET_BY_IDX(LED_NODE, status_led_gpios, 0),
    GPIO_DT_SPEC_GET_BY_IDX(LED_NODE, status_led_gpios, 1),
    GPIO_DT_SPEC_GET_BY_IDX(LED_NODE, status_led_gpios, 2),
};

static const uint8_t profile_colors[] = {BLUE, GREEN | BLUE, RED | BLUE};

static uint32_t tick;
static bool was_connected;
static uint32_t connected_ticks;

static void status_led_tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(status_led_work, status_led_tick);

static void set_color(uint8_t color) {
    for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
        gpio_pin_set_dt(&leds[i], (color & BIT(i)) != 0);
    }
}

static bool battery_is_low(void) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    if (zmk_usb_is_powered()) {
        return false;
    }
#endif
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
    bool active = zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE;
    uint8_t color = OFF;
    tick++;

    if (zmk_ble_active_profile_is_connected()) {
        if (!was_connected) {
            connected_ticks = 0;
        }
        was_connected = true;
        if (connected_ticks < CONNECTED_SOLID_TICKS) {
            connected_ticks++;
            color = profile_color;
        }
    } else {
        was_connected = false;
        if (active && zmk_ble_active_profile_is_open()) {
            color = tick % 2 == 0 ? profile_color : OFF;
        } else if (active && tick % SEARCHING_PERIOD_TICKS == 0) {
            color = profile_color;
        }
    }

    if (color == OFF && active && tick % LOW_BATTERY_PERIOD_TICKS == 1 && battery_is_low()) {
        color = RED;
    }

    set_color(color);
    k_work_reschedule(&status_led_work, K_MSEC(TICK_MS));
}

static int status_led_init(void) {
    for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
        if (!gpio_is_ready_dt(&leds[i])) {
            LOG_ERR("Status LED pin %d is not ready", (int)i);
            return -ENODEV;
        }
        gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
    }

    k_work_reschedule(&status_led_work, K_MSEC(TICK_MS));
    return 0;
}

SYS_INIT(status_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
