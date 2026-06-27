# k400p-fn-lock-win

Lock Fn keys on Logitech K400+ (Windows).

## Goal

On launch, finds the K400+ via any Logitech HID++ receiver and enables Fn Lock so F1–F12 work without pressing Fn. The setting lasts until reboot.

Alternative to Logitech Options/Options+ without a background process.

K400+ connects through the included **USB dongle** (Unifying/Bolt), not Bluetooth.

## How to use

### Quick apply (recommended)

```powershell
pip install -r requirements.txt
python fn_lock_tool.py --apply
```

Sends Fn Lock to **all Logitech HID++ receivers** and **device slots 1–6 + FF** so it works on different PCs and dongle layouts.

### Autostart

Double-click `install_autostart.bat`. It:

1. Creates a Scheduled Task (`K400pFnLock`) at logon (30 s delay) and on session unlock
2. Verifies the task was registered
3. Runs a live test if the dongle is connected

The task runs `apply_fn_lock.ps1` → `fn_lock_tool.py`, with retries and logging to `%LOCALAPPDATA%\k400p-fn-lock\apply.log`.

To verify later: `verify_autostart.bat`

To diagnose: `diagnose_fn_lock.ps1` or `python fn_lock_tool.py --diagnose`

To remove autostart:

```powershell
powershell -ExecutionPolicy Bypass -File install_autostart.ps1 -Uninstall
```

### Standalone exe (optional)

After building, `dist\k400p-fn-lock.exe` does the same HID++ sweep without Python.

## Project layout

```
fn_lock_tool.py          # main tool (Python + hidapi)
apply_fn_lock.ps1        # autostart worker (retries, logging)
install_autostart.*      # scheduled task installer
verify_autostart.bat     # check setup
diagnose_fn_lock.ps1     # device listing + diagnostic apply
main.c / build.bat       # optional native exe
hidapi/                  # for building the exe
requirements.txt         # pip: hidapi
```

## How to build exe

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
