# k400p-fn-lock-win

Lock Fn keys on Logitech K400+ (Windows).

## Goal

On launch, finds the K400+ keyboard and enables Fn Lock so F1–F12 work without pressing Fn. The setting lasts until reboot.

Single standalone `k400p-fn-lock.exe` — no extra DLLs.

Alternative to Logitech Options/Options+ without a background process.

## How to use

Run `k400p-fn-lock.exe` after connecting the keyboard via Bluetooth.

To run at login, use `add_startup.bat` — it adds a shortcut to the Windows Startup folder.

## How to build

MSVC (Developer Command Prompt):

```
build.bat
```

GCC (MinGW):

```
gcc main.c hidapi/windows/hid.c -o dist/k400p-fn-lock.exe -I hidapi/include -I hidapi/windows -lsetupapi
```

## Inspiration

- code from: https://github.com/dheygere/k380-fn-lock-for-windows
- values from: https://github.com/sginne/fn_key_k400_for_logitech
