@echo off
setlocal
cd /d "%~dp0"

echo.
echo  K400+ Fn Lock - verify setup
echo  ============================
echo.

set FAIL=0
set TASK=K400pFnLock
set TOOL=%~dp0fn_lock_tool.py
set PS1=%~dp0apply_fn_lock.ps1
set LOG=%LOCALAPPDATA%\k400p-fn-lock\apply.log

if not exist "%TOOL%" (
    echo [FAIL] fn_lock_tool.py not found: %TOOL%
    set FAIL=1
) else (
    echo [ OK ] fn_lock_tool.py found
)

if not exist "%PS1%" (
    echo [FAIL] apply_fn_lock.ps1 not found: %PS1%
    set FAIL=1
) else (
    echo [ OK ] apply_fn_lock.ps1 found
)

where python >nul 2>&1
if errorlevel 1 (
    echo [FAIL] Python not found in PATH
    set FAIL=1
) else (
    echo [ OK ] Python found
)

schtasks /Query /TN "%TASK%" >nul 2>&1
if errorlevel 1 (
    echo [FAIL] Scheduled task "%TASK%" not found. Run install_autostart.bat first.
    set FAIL=1
) else (
    echo [ OK ] Scheduled task "%TASK%" exists
    schtasks /Query /TN "%TASK%" /FO LIST /V | findstr /I "Task To Run Status Last Result"
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
  "python '%~dp0fn_lock_tool.py' --probe; if ($LASTEXITCODE -eq 0) { Write-Host '[ OK ] Logitech HID++ interface detected'; exit 0 } else { Write-Host '[WARN] HID++ interface not ready (cannot run live test)'; exit 2 }"
set KB_CODE=%ERRORLEVEL%

if %KB_CODE%==0 (
    echo.
    echo Running live Fn Lock test...
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PS1%"
    if errorlevel 1 (
        echo [FAIL] Live test failed. See log: %LOG%
        set FAIL=1
    ) else (
        echo [ OK ] Live test passed - Fn Lock applied
    )
)

echo.
if %FAIL%==0 (
    echo Verification passed.
) else (
    echo Verification failed.
)
echo Log: %LOG%
echo.
pause
exit /b %FAIL%
