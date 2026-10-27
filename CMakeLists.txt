if(CONFIG_MACROPAD_LEDS)
    target_sources(app PRIVATE src/macropad_leds.c)
endif()

if(CONFIG_MACROPAD_STATUS_RPC)
    target_sources(app PRIVATE src/macropad_status.c)
endif()

if(CONFIG_MACROPAD_STATUS_LED)
    target_sources(app PRIVATE src/macropad_status_led.c)
endif()

if(CONFIG_MACROPAD_BT_LED)
    target_sources(app PRIVATE src/macropad_bt_led.c)
endif()

if(CONFIG_MACROPAD_BLE_EXCLUSIVE)
    target_sources(app PRIVATE src/macropad_ble_exclusive.c)
endif()

if(CONFIG_MACROPAD_VIA)
    target_sources(app PRIVATE
        src/via/via_protocol.c
        src/via/via_usb_hid.c
        src/via/via_keycodes.c
        src/via/via_macros.c
        src/via/behavior_via_macro.c
    )
    if(CONFIG_MACROPAD_VIA_BLE)
        target_sources(app PRIVATE src/via/via_ble_hid.c)
    endif()
endif()

if(CONFIG_MACROPAD_BOOT_KEY)
    target_sources(app PRIVATE src/macropad_boot_key.c)
endif()

if(CONFIG_MACROPAD_IR)
    target_include_directories(app PRIVATE include)
    target_sources(app PRIVATE
        src/ir/ir_tx.c
        src/ir/ir_codes.c
        src/ir/behavior_ir.c
    )
endif()
