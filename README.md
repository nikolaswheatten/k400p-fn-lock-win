# k400p-fn-lock-win

Lock Fn keys on Logitech K400+ (Windows).

## Goal

Finds the K400+ via any Logitech HID++ receiver and enables Fn Lock so F1–F12 work without pressing Fn. The setting lasts until reboot.

Single standalone `k400p-fn-lock.exe` — no Python, no DLLs, no background process.

K400+ connects through the included **USB dongle** (Unifying/Bolt), not Bluetooth.

## Download

Prebuilt releases: https://github.com/nikolaswheatten/k400p-fn-lock-win/releases

## Usage

| Command | Description |
|---------|-------------|
| `k400p-fn-lock.exe` | Apply Fn Lock quietly |
| `k400p-fn-lock.exe --diagnose` | Apply with verbose output |
| `k400p-fn-lock.exe --probe` | Check if HID++ receiver is present |
| `k400p-fn-lock.exe --install` | Autostart at logon + session unlock |
| `k400p-fn-lock.exe --uninstall` | Remove autostart |

Fn Lock is sent to **all Logitech HID++ receivers** and **device slots 1–6 + FF** so it works on different PCs and dongle layouts.

Logs: `%LOCALAPPDATA%\k400p-fn-lock\apply.log` and `install.log`

## Build

MSVC (Developer Command Prompt):

```
build.bat
```

Output: `dist\k400p-fn-lock.exe` (static CRT, no redistributable needed)

GCC (MinGW):

```
gcc main.c hidapi/windows/hid.c -o dist/k400p-fn-lock.exe -I hidapi/include -I hidapi/windows -lsetupapi -O2
```

## Inspiration

- code from: https://github.com/dheygere/k380-fn-lock-for-windows
- values from: https://github.com/sginne/fn_key_k400_for_logitech
