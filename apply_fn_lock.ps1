#Requires -Version 5.1
<#
.SYNOPSIS
  Waits for a Logitech HID++ receiver and applies Fn Lock on all device slots.

.EXIT CODES
  0 - Fn Lock applied successfully
  1 - fn_lock_tool.py or Python not found / hidapi missing
  2 - HID++ interface not detected within timeout
  3 - apply failed after all retries
#>
param(
    [int]$MaxWaitMinutes = 10,
    [int]$RetryIntervalSeconds = 15
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ToolPath = Join-Path $ScriptDir "fn_lock_tool.py"
$LogDir = Join-Path $env:LOCALAPPDATA "k400p-fn-lock"
$LogFile = Join-Path $LogDir "apply.log"

function Write-Log {
    param(
        [string]$Message,
        [ValidateSet("INFO", "WARN", "ERROR", "OK")][string]$Level = "INFO"
    )

    if (-not (Test-Path $LogDir)) {
        New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
    }

    $line = "{0:yyyy-MM-dd HH:mm:ss} [{1}] {2}" -f (Get-Date), $Level, $Message
    Add-Content -Path $LogFile -Value $line -Encoding UTF8
}

function Get-PythonCommand {
    foreach ($name in @("python", "python3", "py")) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) {
            return $cmd.Source
        }
    }
    return $null
}

function Test-HidApi {
    param([string]$PythonPath)

    & $PythonPath -c "import hid" 2>$null
    return $LASTEXITCODE -eq 0
}

function Ensure-HidApi {
    param([string]$PythonPath)

    if (Test-HidApi -PythonPath $PythonPath) {
        return $true
    }

    Write-Log "hidapi not installed, running: pip install -r requirements.txt"
    $reqFile = Join-Path $ScriptDir "requirements.txt"
    if (Test-Path $reqFile) {
        & $PythonPath -m pip install -r $reqFile --quiet
    } else {
        & $PythonPath -m pip install hidapi --quiet
    }

    if ($LASTEXITCODE -ne 0) {
        Write-Log "pip install failed" "ERROR"
        return $false
    }

    return (Test-HidApi -PythonPath $PythonPath)
}

function Test-HidppReady {
    param(
        [string]$PythonPath,
        [string]$ToolScript
    )

    & $PythonPath $ToolScript --probe 2>$null
    return $LASTEXITCODE -eq 0
}

function Wait-ForHidpp {
    param(
        [string]$PythonPath,
        [string]$ToolScript,
        [datetime]$Deadline
    )

    while ((Get-Date) -lt $Deadline) {
        if (Test-HidppReady -PythonPath $PythonPath -ToolScript $ToolScript) {
            return $true
        }

        Write-Log "Logitech HID++ interface not ready, waiting ${RetryIntervalSeconds}s..."
        Start-Sleep -Seconds $RetryIntervalSeconds
    }

    return $false
}

function Invoke-FnLockTool {
    param(
        [string]$PythonPath,
        [string]$ToolScript
    )

    $output = & $PythonPath $ToolScript --apply --quiet 2>&1 | Out-String
    $exitCode = $LASTEXITCODE

    foreach ($line in ($output -split "`r?`n")) {
        if ($line.Trim()) {
            Write-Log $line.Trim()
        }
    }

    return $exitCode
}

Write-Log "=== Fn Lock apply started (universal: all receivers, slots 1-6 + FF) ==="

if (-not (Test-Path -LiteralPath $ToolPath)) {
    Write-Log "fn_lock_tool.py not found: $ToolPath" "ERROR"
    exit 1
}

$pythonPath = Get-PythonCommand
if (-not $pythonPath) {
    Write-Log "Python not found. Install Python 3 from python.org" "ERROR"
    exit 1
}

Write-Log "Python: $pythonPath"
Write-Log "Tool: $ToolPath"

if (-not (Ensure-HidApi -PythonPath $pythonPath)) {
    Write-Log "hidapi module unavailable. Run: pip install -r requirements.txt" "ERROR"
    exit 1
}

$deadline = (Get-Date).AddMinutes($MaxWaitMinutes)
Write-Log "Waiting up to $MaxWaitMinutes min for Logitech HID++ receiver..."

if (-not (Wait-ForHidpp -PythonPath $pythonPath -ToolScript $ToolPath -Deadline $deadline)) {
    Write-Log "Logitech HID++ interface not found before timeout" "ERROR"
    exit 2
}

Write-Log "Logitech HID++ interface ready" "OK"

$attempt = 0
while ((Get-Date) -lt $deadline) {
    $attempt++
    Write-Log "Attempt $attempt : fn_lock_tool.py --apply --quiet"

    $exitCode = Invoke-FnLockTool -PythonPath $pythonPath -ToolScript $ToolPath
    Write-Log "fn_lock_tool.py exit code: $exitCode"

    if ($exitCode -eq 0) {
        Write-Log "Fn Lock applied successfully" "OK"
        exit 0
    }

    Write-Log "Fn Lock failed, retrying in ${RetryIntervalSeconds}s..." "WARN"
    Start-Sleep -Seconds $RetryIntervalSeconds

    if (-not (Test-HidppReady -PythonPath $pythonPath -ToolScript $ToolPath)) {
        Write-Log "HID++ interface lost, waiting for reconnect..."
        if (-not (Wait-ForHidpp -PythonPath $pythonPath -ToolScript $ToolPath -Deadline $deadline)) {
            break
        }
        Write-Log "HID++ interface back" "OK"
    }
}

Write-Log "Fn Lock could not be applied after $attempt attempt(s)" "ERROR"
exit 3
