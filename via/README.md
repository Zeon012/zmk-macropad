# VIA on the macropad (experimental)

The `via-compat` branch lets [VIA](https://usevia.app) edit the pad, next to the configurator. The firmware answers VIA's raw HID protocol (`src/via/`) over USB, and over Bluetooth where the browser can reach it.

## Loading the pad in VIA

VIA only knows keyboards from its own list, so it needs this folder's definition once:

1. Open https://usevia.app in Chrome or Edge.
2. **Settings**: turn on **Show Design tab**.
3. **Design**: load `macropad.json` from this folder.
4. **Configure**: authorize the device. Pick the entry for the pad's raw HID interface if the browser lists more than one.

## Editing

- **Unlock the pad first** (hold Game Mode and press Print Screen). VIA has no lock of its own, so the firmware only accepts changes while ZMK Studio is unlocked. VIA can show the keymap while locked, but changes made then are dropped and come back on reload.
- Changes save on their own about a second after the last one; VIA has no Save button.
- **Macros**: VIA's Macros tab writes up to 16 macros (512 bytes in all). Put one on a key with **M0**-**M15** under Special. They type as on a US layout.
- **Knob**: shown in the top-left; each layer's turns can be set.

## What VIA can and cannot show

VIA uses QMK keycodes. These translate both ways: letters and other keys, keys held with modifiers, media and system keys, the layer keys MO/TG/TO, M0-M15, bootloader and reset, and these extras under **Custom**: BT 1-3, BT Clear, USB/BT and Unlock.

Anything else (the configurator's macros, mouse scroll, RGB, two-handed modifier combinations) shows as **ZMK**. Leaving a key on ZMK keeps it, and choosing ZMK for a key changes nothing. Set those keys in the configurator instead.

The key tester in VIA stays blank, as it does on QMK boards; the configurator has one.
