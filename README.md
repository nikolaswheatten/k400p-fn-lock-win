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

`--install` registers a scheduled task with three triggers: logon, session unlock, and a silent reapply every 15 minutes. The periodic trigger exists because Fn Lock is a volatile setting held by the receiver/keyboard firmware (not read back or verified — it's just resent), and it can drop after sleep/resume or a receiver reconnect that isn't covered by the other two triggers.

The task is registered by writing a Task Scheduler XML file and loading it with `schtasks.exe` (no PowerShell involved) — composing the same three triggers through the `ScheduledTasks` PowerShell module was found to silently drop the session-unlock trigger on some machines despite reporting success, so `--install` now reads the task back from Task Scheduler afterwards and fails loudly if a trigger is missing.

Logs and support files: `%LOCALAPPDATA%\k400p-fn-lock\apply.log`, `install.log`, `task.xml`, `task_verify.xml`

## Build

MSVC (Developer Command Prompt):

```
build.bat
```

Output: `dist\k400p-fn-lock.exe` (static CRT, no redistributable needed)

GCC (MinGW):

```
gcc main.c hidapi/windows/hid.c -o dist/k400p-fn-lock.exe -I hidapi/include -I hidapi/windows -lsetupapi -mwindows -O2
```

Built as a GUI-subsystem binary (`-mwindows` / `/SUBSYSTEM:WINDOWS`) even though it's a CLI tool: a console-subsystem exe gets a console window allocated by Windows unconditionally, which flashed on screen every time the scheduled task ran. The binary attaches to the calling terminal's console on startup when there is one, so interactive use (`--diagnose`, `--help`, ...) is unaffected.

## Inspiration

- code from: https://github.com/dheygere/k380-fn-lock-for-windows
- values from: https://github.com/sginne/fn_key_k400_for_logitech
