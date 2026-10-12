Portable Windows app for configuring the macropad. No install: download `Macropad-Configurator-Portable.exe` and run it.

## What it does

- Remap every key on every layer, live, and save the result to the pad.
- Create and edit macros: typed text, or key combos with pauses.
- Set what the knob does per layer, including mouse scrolling.
- Test keys: each press lights up on a drawing of the pad.
- Show battery level and Bluetooth state.

## Requirements

- Windows 10 or 11, 64-bit.
- A macropad running the firmware from this repository's `main` branch. It will not work with stock ZMK firmware, because it relies on the modules this firmware adds.

## Connecting

- **USB:** plug the pad in and click **Connect USB**.
- **Bluetooth:** pair the pad in Windows first, unlock it, then click **Connect Bluetooth**. This path has had little testing on Windows; use USB if it fails.

To unlock the pad for editing, hold Game Mode and press Print Screen.

## Notes

- The app is not code-signed, so Windows SmartScreen may warn on first run. Choose "More info", then "Run anyway".
- Key changes apply immediately and need **Save** to survive a restart. Macros and knob changes are saved as soon as you make them.
- Macros are stored on the pad without encryption, and pressing a macro key types its text on whatever the pad is connected to. Do not put passwords in macros.
