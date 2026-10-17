# VIA on the macropad (experimental)

The `via-compat` branch lets [VIA](https://usevia.app) edit the pad, next to the configurator. The firmware answers VIA's raw HID protocol (`src/via/`) on a second HID interface, over the USB cable and over Bluetooth. Each reply goes back the way its request came, so having both connected at once is fine.

## Loading the pad in VIA

VIA only knows keyboards from its own list, so it needs this folder's definition once:

1. Open https://usevia.app in Chrome or Edge.
2. **Settings**: turn on **Show Design tab**.
3. **Design**: load `macropad.json` from this folder.
4. **Configure**: authorize the pad. Over USB it is listed once; over Bluetooth the browser lists the pad's Bluetooth connection.

**Bluetooth:** the VIA interface is a new Bluetooth service, and Windows remembers a paired device's old service list. If the pad doesn't show up in VIA over Bluetooth, remove it in Windows Bluetooth settings and pair it again. If Windows can't pair, the pad still remembers the old pairing: on the pad, press BT Clear (Fn + key 5) on that Bluetooth profile, then pair.

## Editing

- **Unlock the pad first** (hold Game Mode and press Print Screen). VIA has no lock of its own, so the firmware only accepts changes while ZMK Studio is unlocked. VIA can show the keymap while locked, but changes made then are dropped and come back on reload.
- Changes save on their own about a second after the last one; VIA has no Save button.
- **Macros**: VIA's Macros tab writes up to 16 macros (512 bytes in all). Put one on a key with **M0**-**M15** under Special. They type as on a US layout.
- **Knob**: the round knob top-left; click it to set what turning it each way does, per layer.

## What VIA can and cannot show

VIA uses QMK keycodes. These translate both ways: letters and other keys, keys held with modifiers, media and system keys, the layer keys MO/TG/TO, M0-M15, bootloader and reset, and these extras under **Custom**: BT 1-3, BT Clear, USB/BT and Unlock.

Anything else (the configurator's macros, mouse scroll, RGB, two-handed modifier combinations) shows as **ZMK**. Leaving a key on ZMK keeps it, and choosing ZMK for a key changes nothing. Set those keys in the configurator instead.

VIA's **Test Matrix** (key tester) lights up keys as you press them while the pad is unlocked. While it is locked every key reads as up, so no other program can read keystrokes through VIA.
