/*
 * Hold one key while the pad powers up (plugging in USB, or switching on) to
 * restart into the UF2 bootloader, without opening the case for the reset
 * pins. It reads that switch directly, before the key scanner, USB, Bluetooth
 * or any feature module starts, so it works even if one of those misbehaves.
 *
 * The key is given by its kscan row and column index. With col2row diodes the
 * column is driven high and the row reads high while the key is down.
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

static const struct gpio_dt_spec column =
    GPIO_DT_SPEC_GET_BY_IDX(KSCAN, col_gpios, CONFIG_MACROPAD_BOOT_KEY_COL);
static const struct gpio_dt_spec row =
    GPIO_DT_SPEC_GET_BY_IDX(KSCAN, row_gpios, CONFIG_MACROPAD_BOOT_KEY_ROW);

static bool boot_key_down(void) {
    if (!gpio_is_ready_dt(&column) || !gpio_is_ready_dt(&row) ||
        gpio_pin_configure_dt(&row, GPIO_INPUT) ||
        gpio_pin_configure_dt(&column, GPIO_OUTPUT_ACTIVE)) {
        return false;
    }
    k_busy_wait(100);
    bool down = true;
    /* Ten reads a millisecond apart, so one bounce or glitch cannot trigger it. */
    for (int i = 0; i < 10 && down; i++) {
        down = gpio_pin_get_dt(&row) == 1;
        k_busy_wait(1000);
    }
    /* Hand both pins back as plain inputs; the key scanner sets them up afterwards. */
    gpio_pin_configure_dt(&column, GPIO_INPUT);
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
