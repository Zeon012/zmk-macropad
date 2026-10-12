if(CONFIG_MACROPAD_LEDS)
    target_sources(app PRIVATE src/macropad_leds.c)
endif()

if(CONFIG_MACROPAD_STATUS_RPC)
    target_sources(app PRIVATE src/macropad_status.c)
endif()

if(CONFIG_MACROPAD_STATUS_LED)
    target_sources(app PRIVATE src/macropad_status_led.c)
endif()
