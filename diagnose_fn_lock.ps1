#Requires -Version 5.1
<#
.SYNOPSIS
  Diagnose K400+ Fn Lock: lists HID++ interfaces and tests universal apply.

.EXIT CODES
  0 - Fn Lock apply/diagnose succeeded
  1 - prerequisites missing
  2 - no Logitech HID++ interface found
#>
param(
    [switch]$Apply
)

$ErrorActionPreference = "Stop"

function Write-Section {
    param([string]$Title)
    Write-Host ""
    Write-Host "=== $Title ===" -ForegroundColor Cyan
}

Write-Section "Active keyboards"
Get-PnpDevice -Class Keyboard -ErrorAction SilentlyContinue |
    Where-Object { $_.Status -eq "OK" } |
    Format-Table FriendlyName, InstanceId -AutoSize

Write-Section "Logitech USB devices (OK)"
Get-PnpDevice -ErrorAction SilentlyContinue |
    Where-Object { $_.Status -eq "OK" -and $_.InstanceId -match "VID_046D" } |
    Format-Table FriendlyName, InstanceId -AutoSize

$python = Get-Command python, python3, py -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $python) {
    Write-Host "Python not found. Install Python 3 and run: pip install -r requirements.txt" -ForegroundColor Red
    exit 1
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$pyScript = Join-Path $scriptDir "fn_lock_tool.py"
$reqFile = Join-Path $scriptDir "requirements.txt"

if (-not (Test-Path $pyScript)) {
    Write-Host "Missing $pyScript" -ForegroundColor Red
    exit 1
}

& $python.Source -c "import hid" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "Installing hidapi..."
    if (Test-Path $reqFile) {
        & $python.Source -m pip install -r $reqFile --quiet
    } else {
        & $python.Source -m pip install hidapi --quiet
    }
}

Write-Section "Universal HID++ Fn Lock"
Write-Host "Strategy: all Logitech HID++ receivers, device slots 1-6 + direct (FF)"
Write-Host ""

if ($Apply) {
    & $python.Source $pyScript --apply
} else {
    & $python.Source $pyScript --diagnose
}

exit $LASTEXITCODE
