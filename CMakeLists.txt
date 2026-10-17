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

if(CONFIG_MACROPAD_VIA)
    target_sources(app PRIVATE
        src/via/via_protocol.c
        src/via/via_usb_hid.c
        src/via/via_keycodes.c
        src/via/via_macros.c
        src/via/behavior_via_macro.c
    )
endif()

if(CONFIG_MACROPAD_BOOT_KEY)
    target_sources(app PRIVATE src/macropad_boot_key.c)
endif()
