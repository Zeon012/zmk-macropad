Android app for configuring ZMK devices built from this project: the macropad and the KB60 keyboard. It connects over Bluetooth. Download `Macropad-Configurator.apk` on your phone and open it to install. Android asks once to allow installing apps from your browser or file manager.

## New in 1.0.5

- **Runs in landscape**, either way up, so a full keyboard like the KB60 has room. The top bar is compact on a sideways phone and clears the status bar.
- **Readable keycaps** on a full keyboard: short names (Bksp, Del, Ctrl, Shift, Caps) and text that shrinks to fit small keys instead of breaking mid-word.

## New in 1.0.3

- **ANDROID key tab** for a keyboard or pad paired to your phone: keys that open the browser, email, maps, calculator, settings and the assistant, and keys for Home, Back, recent apps, all apps, notifications, lock, screenshot and the shortcut list. They use Android's own keyboard shortcuts, so no firmware update is needed.
- **Program macros** from the desktop configurator: a macro that opens a Windows program through Win+R, optionally as administrator, with lists of apps, Windows tools and RSAT server tools. These launch programs on a Windows PC, not on the phone.

## New in 1.0.2

- **Bluetooth Off** can be assigned to a key from the DEVICE tab, as in the desktop configurator. Pressing it disconnects the computer; selecting Bluetooth profile 1 reconnects it.

## New in 1.0.1

- **Macros, the knob, the key tester and battery status now show up over Bluetooth.** The app asks the device which of these it supports right after connecting, and over a phone's Bluetooth link the answer could arrive too late, so they were switched off. It now waits long enough.

## New in 1.0.0

- First Android version. Everything the desktop configurator does over Bluetooth works on the phone: remap keys on every layer, macros, the knob, the key tester, and battery and Bluetooth status.
- Layout made for phone screens.

## Connecting

1. Pair the device with the phone in Android's Bluetooth settings.
2. Unlock it: on the macropad, hold Game Mode and press Print Screen; on the KB60, hold Fn and press `\`.
3. In the app, pick the device and tap **Connect Bluetooth**. Allow the Bluetooth permission the first time.

## Requirements

Android 7.0 or newer with Bluetooth LE.
