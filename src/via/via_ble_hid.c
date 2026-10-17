/*
 * VIA over Bluetooth: a second HID service (HID over GATT) with the same raw
 * HID report descriptor as the USB interface. The host sees it as another
 * HID device on the pad's Bluetooth connection, so the VIA app in Chrome or
 * Edge can open it the same way as over USB.
 *
 * Requests arrive as writes to the output report; the reply is notified on
 * the input report of the same connection only. Both need an encrypted
 * (paired) link, like the pad's keyboard service.
 *
 * Adding a service changes the pad's GATT table, so a computer that cached
 * the old one may need the pad removed and paired again before it shows up.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "via.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define HIDS_REMOTE_WAKE BIT(0)
#define HIDS_NORMALLY_CONNECTABLE BIT(1)
#define HIDS_INPUT 0x01
#define HIDS_OUTPUT 0x02

struct hids_info {
    uint16_t version;
    uint8_t country_code;
    uint8_t flags;
} __packed;

struct hids_report_ref {
    uint8_t id;
    uint8_t type;
} __packed;

static const struct hids_info info = {
    .version = 0x0111,
    .country_code = 0,
    .flags = HIDS_NORMALLY_CONNECTABLE,
};

/* The raw HID report descriptor has no report IDs, so both references use 0. */
static const struct hids_report_ref input_ref = {.id = 0, .type = HIDS_INPUT};
static const struct hids_report_ref output_ref = {.id = 0, .type = HIDS_OUTPUT};

static uint8_t last_reply[VIA_REPORT_SIZE];
static uint8_t control_point;

static ssize_t read_info(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                         uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &info, sizeof(info));
}

static ssize_t read_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                               uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, via_raw_hid_report_desc,
                             via_raw_hid_report_desc_len);
}

static ssize_t read_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                               uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_report_ref));
}

static ssize_t read_input_report(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                                 uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, last_reply, sizeof(last_reply));
}

static ssize_t write_output_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                   const void *buf, uint16_t len, uint16_t offset,
                                   uint8_t flags) {
    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    via_receive_report(buf, len, VIA_TRANSPORT_BLE, bt_conn_ref(conn));
    return len;
}

static ssize_t write_control_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                   const void *buf, uint16_t len, uint16_t offset,
                                   uint8_t flags) {
    if (offset + len > sizeof(control_point)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    memcpy(&control_point + offset, buf, len);
    return len;
}

/* attrs[5] is the input report's characteristic, notified with each reply. */
BT_GATT_SERVICE_DEFINE(
    via_hids_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_info,
                           NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT,
                           read_report_map, NULL, NULL),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_input_report, NULL, NULL),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_report_ref, NULL,
                       (void *)&input_ref),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT, NULL,
                           write_output_report, NULL),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_report_ref, NULL,
                       (void *)&output_ref),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, write_control_point, &control_point));

void via_ble_send_report(void *conn, const uint8_t *report) {
    memcpy(last_reply, report, sizeof(last_reply));
    struct bt_gatt_notify_params params = {
        .attr = &via_hids_svc.attrs[5],
        .data = last_reply,
        .len = sizeof(last_reply),
    };
    int err = bt_gatt_notify_cb(conn, &params);
    if (err) {
        LOG_WRN("Sending a VIA reply over Bluetooth failed (%d)", err);
    }
    bt_conn_unref(conn);
}

void via_ble_release(void *conn) { bt_conn_unref(conn); }
