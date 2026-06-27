@echo off
setlocal
cd /d "%~dp0"

echo.
echo  K400+ Fn Lock - install autostart with verification
echo  ====================================================
echo.

where powershell >nul 2>&1
if errorlevel 1 (
    echo ERROR: PowerShell not found.
    pause
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_autostart.ps1"
set EXIT_CODE=%ERRORLEVEL%

echo.
if %EXIT_CODE%==0 (
    echo Installation completed successfully.
) else (
    echo Installation finished with errors. Exit code: %EXIT_CODE%
    echo Log: %%LOCALAPPDATA%%\k400p-fn-lock\install.log
)
echo.
pause
exit /b %EXIT_CODE%
