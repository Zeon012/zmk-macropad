/*
 * Only the selected Bluetooth profile stays connected. ZMK normally keeps the
 * other hosts connected after a profile switch (they just get no keystrokes),
 * so a computer you switched away from still shows the pad as connected. This
 * drops every other profile's connection whenever the selected one changes,
 * and again if one of those hosts connects back. Switching back reconnects
 * that host, which takes a second or two.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static void disconnect_inactive(struct k_work *work) {
    int active = zmk_ble_active_profile_index();
    for (uint8_t index = 0; index < ZMK_BLE_PROFILE_COUNT; index++) {
        if (index == active) {
            continue;
        }
        int err = zmk_ble_prof_disconnect(index);
        if (err && err != -ENODEV) {
            LOG_WRN("Disconnecting inactive profile %d failed (%d)", index, err);
        }
    }
}

static K_WORK_DEFINE(disconnect_work, disconnect_inactive);

/*
 * ZMK advertises to everyone while the selected profile has no connection
 * (and while Studio is unlocked), so a host just dropped can connect straight
 * back. Drop it again whenever a host from another profile connects or its
 * link is secured (when its real address becomes known). A device that is
 * pairing belongs to no profile yet and is left alone.
 */
static void check_connection(struct bt_conn *conn) {
    int index = zmk_ble_profile_index(bt_conn_get_dst(conn));
    if (index >= 0 && index != zmk_ble_active_profile_index()) {
        k_work_submit(&disconnect_work);
    }
}

static void connected(struct bt_conn *conn, uint8_t err) {
    if (!err) {
        check_connection(conn);
    }
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err) {
    if (!err) {
        check_connection(conn);
    }
}

BT_CONN_CB_DEFINE(macropad_ble_exclusive_conn_callbacks) = {
    .connected = connected,
    .security_changed = security_changed,
};

static int profile_listener(const zmk_event_t *eh) {
    if (as_zmk_ble_active_profile_changed(eh)) {
        /* Not from the event itself: it can be raised inside the Bluetooth stack's callbacks. */
        k_work_submit(&disconnect_work);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(macropad_ble_exclusive, profile_listener);
ZMK_SUBSCRIPTION(macropad_ble_exclusive, zmk_ble_active_profile_changed);
