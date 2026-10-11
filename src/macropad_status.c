/*
 * Battery and power status for the configurator, as a custom Studio RPC
 * subsystem. The payload is raw bytes rather than a protobuf message:
 *
 *   request:  [command]            command 1 = get status
 *   response: [version, battery_percent, flags]
 *             flags bit 0 = USB power present
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

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STATUS_VERSION 1
#define COMMAND_GET_STATUS 1
#define FLAG_USB_POWERED BIT(0)

/* The level is not sensitive, so it can be read while Studio is locked. */
static struct zmk_rpc_custom_subsystem_meta macropad_status_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS(),
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

ZMK_RPC_CUSTOM_SUBSYSTEM(macropad__status, &macropad_status_meta, macropad_status_handle_request);

/* The response is encoded after the handler returns, possibly more than once,
 * so it has to live in static storage. */
static uint8_t response[3];

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

    response[0] = STATUS_VERSION;
    response[1] = zmk_battery_state_of_charge();
    response[2] = flags;

    encode_response->funcs.encode = encode_response_payload;
    encode_response->arg = NULL;
    return true;
}
