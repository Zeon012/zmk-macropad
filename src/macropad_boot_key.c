/*
 * Hold one key while the pad powers up (plugging in USB, or switching on) to
 * restart into the UF2 bootloader, without opening the case for the reset
 * pins. It reads that switch directly, before the key scanner, USB, Bluetooth
 * or any feature module starts, so it works even if one of those misbehaves.
 *
 * The key is given by its kscan row and column index, or with
 * MACROPAD_BOOT_KEY_ANY any key works. The column is driven active and the row
 * reads active while the key is down (col2row).
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#include <dt-bindings/zmk/reset.h>

#if IS_ENABLED(CONFIG_RETENTION_BOOT_MODE)
#include <zephyr/retention/bootmode.h>
#endif

#define KSCAN DT_CHOSEN(zmk_kscan)

#if IS_ENABLED(CONFIG_MACROPAD_BOOT_KEY_ANY)
/* Any key: every column driven at once, every row read. */
#define SPEC(node, prop, idx) GPIO_DT_SPEC_GET_BY_IDX(node, prop, idx),
static const struct gpio_dt_spec columns[] = {DT_FOREACH_PROP_ELEM(KSCAN, col_gpios, SPEC)};
static const struct gpio_dt_spec rows[] = {DT_FOREACH_PROP_ELEM(KSCAN, row_gpios, SPEC)};
#else
static const struct gpio_dt_spec columns[] = {
    GPIO_DT_SPEC_GET_BY_IDX(KSCAN, col_gpios, CONFIG_MACROPAD_BOOT_KEY_COL)};
static const struct gpio_dt_spec rows[] = {
    GPIO_DT_SPEC_GET_BY_IDX(KSCAN, row_gpios, CONFIG_MACROPAD_BOOT_KEY_ROW)};
#endif

/* Pin flags (active level, pull, open drain) come from devicetree, so this
 * reads active-low matrices too. */
static bool any_row_active(void) {
    for (size_t i = 0; i < ARRAY_SIZE(rows); i++) {
        if (gpio_pin_get_dt(&rows[i]) == 1) {
            return true;
        }
    }
    return false;
}

static bool boot_key_down(void) {
    for (size_t i = 0; i < ARRAY_SIZE(rows); i++) {
        if (!gpio_is_ready_dt(&rows[i]) || gpio_pin_configure_dt(&rows[i], GPIO_INPUT)) {
            return false;
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(columns); i++) {
        if (!gpio_is_ready_dt(&columns[i]) ||
            gpio_pin_configure_dt(&columns[i], GPIO_OUTPUT_ACTIVE)) {
            return false;
        }
    }
    k_busy_wait(100);
    bool down = true;
    /* Ten reads a millisecond apart, so one bounce or glitch cannot trigger it. */
    for (int i = 0; i < 10 && down; i++) {
        down = any_row_active();
        k_busy_wait(1000);
    }
    /* Hand the pins back as plain inputs; the key scanner sets them up afterwards. */
    for (size_t i = 0; i < ARRAY_SIZE(columns); i++) {
        gpio_pin_configure_dt(&columns[i], GPIO_INPUT);
    }
    return down;
}

static bool held_at_boot;

/* After the GPIO drivers, before the key scanner sets up the same pins. */
static int macropad_boot_key_read(void) {
    held_at_boot = boot_key_down();
    return 0;
}

SYS_INIT(macropad_boot_key_read, POST_KERNEL, 0);

/*
 * The boot mode lives in retained memory, which is only ready once every
 * POST_KERNEL driver has started, so the restart waits until here: still
 * before USB, Bluetooth and ZMK's own start-up.
 */
static int macropad_boot_key_restart(void) {
    if (!held_at_boot) {
        return 0;
    }
#if IS_ENABLED(CONFIG_RETENTION_BOOT_MODE)
    if (bootmode_set(BOOT_MODE_TYPE_BOOTLOADER) == 0) {
        sys_reboot(SYS_REBOOT_WARM);
    }
#else
    sys_reboot(RST_UF2);
#endif
    return 0;
}

SYS_INIT(macropad_boot_key_restart, APPLICATION, 0);
