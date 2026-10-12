# zmk-macropad

ZMK firmware (Bluetooth + USB, ZMK Studio) for the 13-key macropad from `codex-micro-device`, moved from the ESP32-S3 to an nRF52840 (nice!nano-compatible) board.

## Wiring (nice!nano v2 pinout, USB at top)

Diodes are unchanged from the original: column bus -|>|- switch - row bus (cathode toward the row), so `diode-direction = "col2row"`.

| Function | nRF pin | Silkscreen |
|---|---|---|
| Row 0 | P0.06 | D1 |
| Row 1 | P0.08 | D0 |
| Row 2 | P0.17 | D2 |
| Row 3 | P0.20 | D3 |
| Col 0 | P0.22 | D4 |
| Col 1 | P0.24 | D5 |
| Col 2 | P1.00 | D6 |
| Col 3 | P0.11 | D7 |
| Encoder A | P1.04 | D8 |
| Encoder B | P1.06 | D9 |
| WS2812 data (23 LEDs) | P1.13 | D15 |

Pins P0.31 (D21) and P0.29 (D20) are claimed by the SPI peripheral used for the LEDs (SCK/MISO) and must stay unconnected.

Key positions (row, col): R0 C1-C2, R1 C0-C3, R2 C0-C3, R3 C1-C3. R0 C0, R0 C3 and R3 C0 are empty.

## RGB power

The WS2812 strip wants 5 V. Power it from USB 5 V, not the 3.3 V pin, and consider a level shifter or a sacrificial first LED on the data line (3.3 V logic is marginal). `AUTO_OFF_USB` turns the LEDs off whenever USB is unplugged, so they do not drain the battery.

## Default keymap (edit `config/macropad.keymap`, or use ZMK Studio)

The original firmware shipped every control as "None", so these are placeholders:

- Base: F13-F22 for the ten task and action keys, Home and Pause/Break on the two bottom keys, Send = Fn layer (hold). Encoder = volume.
- Fn (hold Send): BT select 0-2, BT clear, USB/BT toggle, RGB toggle/effect/brightness, bootloader, Studio unlock (Home key). Encoder = next/previous track.

## Build and flash

1. Push to GitHub; the Action produces `macropad-nice_nano.uf2` and `settings_reset-nice_nano.uf2`.
2. Double-tap reset, then drop `settings_reset` on the `NICENANO` drive, then the macropad `.uf2`.
3. Pair over Bluetooth ("Macropad"), or plug in USB. For ZMK Studio, hold Fn and press the Studio-unlock key, then open https://zmk.studio.
