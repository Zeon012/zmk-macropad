/*
 * Battery and power status for the configurator, as a custom Studio RPC
 * subsystem. The payload is raw bytes rather than a protobuf message:
 *
 *   request:  [command]            command 1 = get status
 *   response: [version, battery_percent, flags, bluetooth_profile,
 *              six bytes of the pad's Bluetooth address, first octet first]
 *             flags bit 0 = USB power present
 *             flags bit 1 = the selected Bluetooth profile's host is connected
 *             flags bit 2 = the selected Bluetooth profile has no host (pairing mode)
 *             flags bit 3 = Bluetooth is available
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <pb_encode.h>

#include <zmk/battery.h>
#include <zmk/studio/custom.h>

#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zephyr/bluetooth/bluetooth.h>
#include <zmk/ble.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STATUS_VERSION 1
#define COMMAND_GET_STATUS 1
#define FLAG_USB_POWERED BIT(0)
#define FLAG_BT_CONNECTED BIT(1)
#define FLAG_BT_PAIRING BIT(2)
#define FLAG_BT_AVAILABLE BIT(3)

/* The level is not sensitive, so it can be read while Studio is locked. */
static struct zmk_rpc_custom_subsystem_meta macropad_status_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS(),
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

ZMK_RPC_CUSTOM_SUBSYSTEM(macropad__status, &macropad_status_meta, macropad_status_handle_request);

/* The response is encoded after the handler returns, possibly more than once,
 * so it has to live in static storage. */
static uint8_t response[10];

static bool encode_response_payload(pb_ostream_t *stream, const pb_field_t *field,
                                    void *const *arg) {
    if (!pb_encode_tag_for_field(stream, field)) {
        return false;
    }
    return pb_encode_string(stream, response, sizeof(response));
}

static bool macropad_status_handle_request(const zmk_custom_CallRequest *request,
                                           pb_callback_t *encode_response) {
    if (request->payload.size < 1 || request->payload.bytes[0] != COMMAND_GET_STATUS) {
        LOG_WRN("Unsupported macropad status request");
        return false;
    }

    uint8_t flags = 0;
#if IS_ENABLED(CONFIG_ZMK_USB)
    if (zmk_usb_is_powered()) {
        flags |= FLAG_USB_POWERED;
    }
#endif

    uint8_t profile = 0;
#if IS_ENABLED(CONFIG_ZMK_BLE)
    flags |= FLAG_BT_AVAILABLE;
    if (zmk_ble_active_profile_is_connected()) {
        flags |= FLAG_BT_CONNECTED;
    } else if (zmk_ble_active_profile_is_open()) {
        flags |= FLAG_BT_PAIRING;
    }
    profile = (uint8_t)zmk_ble_active_profile_index();

    /* The identity address is what a paired host sees, so the configurator
     * can pick out this exact pad when it connects over Bluetooth. */
    bt_addr_le_t identity;
    size_t identity_count = 1;
    bt_id_get(&identity, &identity_count);
    for (size_t i = 0; i < sizeof(identity.a.val); i++) {
        response[4 + i] = identity_count > 0 ? identity.a.val[sizeof(identity.a.val) - 1 - i] : 0;
    }
#endif

    response[0] = STATUS_VERSION;
    response[1] = zmk_battery_state_of_charge();
    response[2] = flags;
    response[3] = profile;

    encode_response->funcs.encode = encode_response_payload;
    encode_response->arg = NULL;
    return true;
}
