# k400p-fn-lock-win

Lock Fn keys on Logitech K400+ (Windows).

## Goal

Finds the K400+ via any Logitech HID++ receiver and enables Fn Lock so F1–F12 work without pressing Fn. The setting lasts until reboot.

Single standalone `k400p-fn-lock.exe` — no Python, no DLLs, no install besides `--install` itself.

K400+ connects through the included **USB dongle** (Unifying/Bolt), not Bluetooth.

## Download

Prebuilt releases: https://github.com/nikolaswheatten/k400p-fn-lock-win/releases

## Usage

| Command | Description |
|---------|-------------|
| `k400p-fn-lock.exe` | Apply Fn Lock quietly |
| `k400p-fn-lock.exe --diagnose` | Apply with verbose output |
| `k400p-fn-lock.exe --probe` | Check if HID++ receiver is present |
| `k400p-fn-lock.exe --install` | Start the background app now, and at every future logon |
| `k400p-fn-lock.exe --uninstall` | Remove autostart and stop the background app |

Fn Lock is sent to **all Logitech HID++ receivers** and **device slots 1–6 + FF** so it works on different PCs and dongle layouts.

### Autostart: a resident background app, not a scheduled task

Fn Lock is a volatile setting held by the receiver/keyboard firmware — it's not read back or verified, only resent — and it can drop after sleep/resume or a receiver reconnect. Earlier versions covered this with a Task Scheduler task that re-launched the exe on a timer. That worked, but every launch is a console-subsystem process, and Windows briefly creates (and can foreground) a console window for it even when nothing is printed — enough to kick a fullscreen game out to the desktop.

`--install` instead adds a registry `Run` entry (`HKCU\...\CurrentVersion\Run`, no admin rights needed) that starts `k400p-fn-lock.exe --resident` once at logon and leaves it running for the session — a small tray icon (right-click for "Reapply now" / "Exit") is the only visible trace. Inside, a hidden message-only window reacts immediately to real OS events — session unlock and resume-from-sleep — instead of guessing on a timer, with a 15-minute timer kept only as a fallback. Because it's one long-lived process instead of a new one every 15 minutes, no window is ever created after the initial (also hidden) logon launch.

`--uninstall` removes the registry entry and signals any running instance to exit. Both commands also clean up the old scheduled task from earlier versions if present.

Logs: `%LOCALAPPDATA%\k400p-fn-lock\apply.log`, `install.log`

## Build

MSVC (Developer Command Prompt):

```
build.bat
```

Output: `dist\k400p-fn-lock.exe` (static CRT, no redistributable needed)

GCC (MinGW):

```
gcc main.c hidapi/windows/hid.c -o dist/k400p-fn-lock.exe -I hidapi/include -I hidapi/windows -lsetupapi -lwtsapi32 -ladvapi32 -O2
```

Windows still allocates a console window for this console-subsystem exe whenever it's launched without one (e.g. the one-time logon launch of `--resident`). On startup the binary checks whether it owns its console exclusively (`GetConsoleProcessList`); if so, nobody else could be using it, meaning Windows just created it for this launch, so it's hidden immediately (`ShowWindow(..., SW_HIDE)`) before it can flash. Run from a terminal, the console is shared with that shell and is left alone, so interactive use (`--diagnose`, `--help`, ...) is unaffected.

## Inspiration

- code from: https://github.com/dheygere/k380-fn-lock-for-windows
- values from: https://github.com/sginne/fn_key_k400_for_logitech
