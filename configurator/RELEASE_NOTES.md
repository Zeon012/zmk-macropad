Portable Windows app for configuring ZMK devices built from this project. No install: download `Macropad-Configurator-Portable.exe` and run it.

## New in 1.0.6

- **Program macros.** A new **Program** type in the macro editor launches a program: pick one from the list (Terminal, Explorer, Task Manager, Settings, VS Code, Chrome and more) or paste any program's full path, and tick **Run as administrator** if needed. The key opens Win+R, types the command and runs it, so it works on any Windows PC without this app running.

## New in 1.0.5

- **Connect USB** also finds the KB60 keyboard, which now has its own USB ID (1209:0001) instead of sharing ZMK's with the macropad.

## New in 1.0.4

- **Bluetooth off** on the DEVICE tab, for firmware that has it: switches to spare Bluetooth profile 3 and disconnects the computer, so it stays disconnected. Bluetooth 1 reconnects.
- Over Bluetooth, the app waits up to 5 seconds for the device to list its extras (macros, knob, key tester, battery), so they no longer go missing on a slow link.

## New in 1.0.3

- **Connect Bluetooth no longer hangs.** If the device is paired but asleep or out of range, the connection now gives up after 10 seconds and says so, instead of waiting forever. The search for the device is also shorter (10 seconds).
- If the device connects over Bluetooth but does not answer, the app closes the link within a couple of seconds and explains what to try, instead of staying on *Connecting...*.
- While connecting, the status reads *Looking for the device over Bluetooth...* and the connect buttons are disabled, so a second click cannot start a second attempt.

## New in 1.0.2

- **Device selector.** Choose Macropad, Keyboard (KB60) or Any device next to **Connect USB**. The app learns each device the first time it connects to it and opens the chosen one directly from then on. If it opens the wrong one before it has learned them, it says so; click **Connect USB** again for the next device.
- Macros, the key tester, battery level and Bluetooth state now also work on the keyboard when its firmware includes them, and **Connect Bluetooth** can find the keyboard as well as the macropad.

## New in 1.0.1

- **Works with other ZMK Studio devices**, not only the macropad. A device other than the macropad is drawn from the layout it reports, with keys at their real sizes and positions. Built for the 64-key KB60 keyboard.
- Features a device's firmware lacks (macros, knob, key tester, battery) switch themselves off.
- When a Bluetooth connection finds nothing, the app now says why: the pad only announces itself while unlocked.
- Right Ctrl, Right Shift, Right Alt, Right Win and Menu are named and available on the SPECIAL tab.

## What it does

- Remap every key on every layer, live, and save the result to the device.
- On the macropad: create and edit macros (typed text, or key combos with pauses), set what the knob does per layer including mouse scrolling, test keys on a drawing of the pad, and see battery level and Bluetooth state.

## Requirements

- Windows 10 or 11, 64-bit.
- The macropad running the firmware from this repository's `main` branch, or another keyboard with ZMK Studio enabled over USB. The macropad's extra features rely on the modules its firmware adds.

## Connecting

- **USB:** plug the device in and click **Connect USB**.
- **Bluetooth:** pair the device in Windows, unlock it, then click **Connect Bluetooth** within a few seconds. Use USB if it fails.

To unlock for editing: on the macropad, hold Game Mode and press Print Screen; on the KB60, hold Fn and press `\`.

## Notes

- The app is not code-signed, so Windows SmartScreen may warn on first run. Choose "More info", then "Run anyway".
- Key changes apply immediately and need **Save** to survive a restart. Macros and knob changes are saved as soon as you make them.
- Macros are stored on the pad without encryption, and pressing a macro key types its text on whatever the pad is connected to. Do not put passwords in macros.
