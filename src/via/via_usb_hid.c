/*
 * VIA's transport: a second USB HID interface with QMK's raw HID report
 * descriptor (usage page 0xFF60, usage 0x61, 32-byte reports), which is how
 * VIA finds a keyboard. VIA writes requests as output reports (SET_REPORT on
 * the control endpoint) and reads replies from the interrupt IN endpoint.
 *
 * via_ble_hid.c is the same interface over Bluetooth. Each reply goes back
 * only over the connection its request came in on.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/class/usb_hid.h>

#include "via.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define HID_REPORT_TYPE_MASK 0xff00
#define HID_REPORT_TYPE_OUTPUT 0x200

const uint8_t via_raw_hid_report_desc[] = {
    0x06, 0x60, 0xFF, /* Usage Page (Vendor 0xFF60) */
    0x09, 0x61,       /* Usage (0x61) */
    0xA1, 0x01,       /* Collection (Application) */
    0x09, 0x62,       /*   Usage (0x62): replies to the host */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x26, 0xFF, 0x00, /*   Logical Maximum (255) */
    0x95, VIA_REPORT_SIZE, /* Report Count */
    0x75, 0x08,       /*   Report Size (8) */
    0x81, 0x02,       /*   Input (Data, Variable, Absolute) */
    0x09, 0x63,       /*   Usage (0x63): requests from the host */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x26, 0xFF, 0x00, /*   Logical Maximum (255) */
    0x95, VIA_REPORT_SIZE, /* Report Count */
    0x75, 0x08,       /*   Report Size (8) */
    0x91, 0x02,       /*   Output (Data, Variable, Absolute) */
    0xC0,             /* End Collection */
};

const size_t via_raw_hid_report_desc_len = sizeof(via_raw_hid_report_desc);

static const struct device *hid_dev;

/* Held while a reply is on its way to the host. */
static K_SEM_DEFINE(in_free, 1, 1);

static void in_ready_cb(const struct device *dev) { k_sem_give(&in_free); }

static int get_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {
    return -ENOTSUP;
}

static int set_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {
    if ((setup->wValue & HID_REPORT_TYPE_MASK) != HID_REPORT_TYPE_OUTPUT) {
        return -ENOTSUP;
    }
    via_receive_report(*data, *len, VIA_TRANSPORT_USB, NULL);
    return 0;
}

static const struct hid_ops ops = {
    .int_in_ready = in_ready_cb,
    .get_report = get_report_cb,
    .set_report = set_report_cb,
};

void via_usb_send_report(const uint8_t *report) {
    /*
     * Static, not on the stack: the USB controller reads it after this
     * returns. The semaphore keeps the next reply from overwriting it early.
     */
    static uint8_t buffer[VIA_REPORT_SIZE];
    if (!hid_dev || k_sem_take(&in_free, K_MSEC(100)) != 0) {
        LOG_WRN("VIA reply dropped: the host is not reading");
        return;
    }
    memcpy(buffer, report, VIA_REPORT_SIZE);
    int err = hid_int_ep_write(hid_dev, buffer, VIA_REPORT_SIZE, NULL);
    if (err) {
        LOG_ERR("Sending a VIA reply failed (%d)", err);
        k_sem_give(&in_free);
    }
}

/* Before ZMK enables USB (CONFIG_ZMK_USB_INIT_PRIORITY, 96 by default). */
static int via_usb_hid_init(void) {
    hid_dev = device_get_binding("HID_1");
    if (!hid_dev) {
        LOG_ERR("No second USB HID device for VIA; set CONFIG_USB_HID_DEVICE_COUNT=2");
        return -ENODEV;
    }
    usb_hid_register_device(hid_dev, via_raw_hid_report_desc, via_raw_hid_report_desc_len, &ops);
    return usb_hid_init(hid_dev);
}

SYS_INIT(via_usb_hid_init, APPLICATION, 90);
