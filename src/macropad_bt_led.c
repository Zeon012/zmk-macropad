/*
 * Bluetooth status on a single LED (the board's onboard LED by default):
 *
 *   fast blink        the selected profile has no host yet: pairing mode
 *   short flash / 2 s paired, but the host is not connected
 *   solid for 1.5 s   just connected, then off
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

#include <zmk/activity.h>
#include <zmk/ble.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define TICK_MS 125
#define CONNECTED_SOLID_TICKS (1500 / TICK_MS)
#define SEARCHING_PERIOD_TICKS (2000 / TICK_MS)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), bt_led_gpios);

static uint32_t tick;
static bool was_connected;
static uint32_t connected_ticks;

static void bt_led_tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(bt_led_work, bt_led_tick);

static void bt_led_tick(struct k_work *work) {
    bool on = false;
    tick++;

    if (zmk_ble_active_profile_is_connected()) {
        if (!was_connected) {
            connected_ticks = 0;
        }
        on = connected_ticks < CONNECTED_SOLID_TICKS;
        connected_ticks++;
        was_connected = true;
    } else {
        was_connected = false;
        if (zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE) {
            on = false;
        } else if (zmk_ble_active_profile_is_open()) {
            on = tick % 2 == 0;
        } else {
            on = tick % SEARCHING_PERIOD_TICKS == 0;
        }
    }

    gpio_pin_set_dt(&led, on);
    k_work_reschedule(&bt_led_work, K_MSEC(TICK_MS));
}

static int bt_led_init(void) {
    if (!gpio_is_ready_dt(&led)) {
        LOG_ERR("Bluetooth status LED is not ready");
        return -ENODEV;
    }

    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    k_work_reschedule(&bt_led_work, K_MSEC(TICK_MS));
    return 0;
}

SYS_INIT(bt_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
