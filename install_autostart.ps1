#Requires -Version 5.1
<#
.SYNOPSIS
  Installs a verified Scheduled Task to apply K400+ Fn Lock at logon and unlock.

.EXIT CODES
  0 - installed and verified
  1 - prerequisite missing
  2 - task registration failed
  3 - verification failed
  4 - test run failed (keyboard connected but Fn Lock not applied)
#>
param(
    [switch]$SkipTestRun,
    [switch]$Uninstall,
    [int]$LogonDelaySeconds = 30
)

$ErrorActionPreference = "Stop"

$TaskName = "K400pFnLock"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ApplyScript = Join-Path $ScriptDir "apply_fn_lock.ps1"
$ToolPath = Join-Path $ScriptDir "fn_lock_tool.py"
$LogDir = Join-Path $env:LOCALAPPDATA "k400p-fn-lock"
$VerifyLog = Join-Path $LogDir "install.log"

function Write-InstallLog {
    param([string]$Message)

    if (-not (Test-Path $LogDir)) {
        New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
    }

    $line = "{0:yyyy-MM-dd HH:mm:ss} {1}" -f (Get-Date), $Message
    Add-Content -Path $VerifyLog -Value $line -Encoding UTF8
    Write-Host $Message
}

function Test-Prerequisites {
    $ok = $true

    if (-not (Test-Path -LiteralPath $ApplyScript)) {
        Write-InstallLog "ERROR: apply_fn_lock.ps1 not found: $ApplyScript"
        $ok = $false
    }

    if (-not (Test-Path -LiteralPath $ToolPath)) {
        Write-InstallLog "ERROR: fn_lock_tool.py not found: $ToolPath"
        $ok = $false
    }

    $python = Get-Command python, python3, py -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $python) {
        Write-InstallLog "ERROR: Python 3 not found. Install from https://python.org"
        $ok = $false
    } else {
        Write-InstallLog "Python: $($python.Source)"
    }

    return $ok
}

function Remove-AutostartTask {
    $existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($existing) {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
        Write-InstallLog "Removed scheduled task '$TaskName'."
    } else {
        Write-InstallLog "Scheduled task '$TaskName' was not installed."
    }
}

function Install-AutostartTask {
    param([int]$DelaySeconds)

    $psArgs = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$ApplyScript`""
    $action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $psArgs

    $triggerLogon = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
    $triggerLogon.Delay = "PT${DelaySeconds}S"

    $unlockClass = Get-CimClass `
        -ClassName MSFT_TaskSessionStateChangeTrigger `
        -Namespace Root/Microsoft/Windows/TaskScheduler
    $triggerUnlock = New-CimInstance -CimClass $unlockClass -ClientOnly
    $triggerUnlock.Enabled = $true
    $triggerUnlock.StateChange = 8   # TASK_SESSION_UNLOCK
    $triggerUnlock.UserId = $env:USERNAME

    $settings = New-ScheduledTaskSettingsSet `
        -AllowStartIfOnBatteries `
        -DontStopIfGoingOnBatteries `
        -StartWhenAvailable `
        -MultipleInstances IgnoreNew

    $principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Limited

    Register-ScheduledTask `
        -TaskName $TaskName `
        -Action $action `
        -Trigger @($triggerLogon, $triggerUnlock) `
        -Settings $settings `
        -Principal $principal `
        -Description "Apply Logitech K400+ Fn Lock after logon/unlock" `
        -Force | Out-Null

    Write-InstallLog "Registered scheduled task '$TaskName' (logon delay ${DelaySeconds}s + session unlock)."
}

function Test-AutostartTask {
    $task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if (-not $task) {
        Write-InstallLog "VERIFY FAIL: task '$TaskName' not found."
        return $false
    }

    if ($task.State -notin @("Ready", "Running")) {
        Write-InstallLog "VERIFY FAIL: task state is '$($task.State)', expected Ready."
        return $false
    }

    $info = Get-ScheduledTaskInfo -TaskName $TaskName
    Write-InstallLog "VERIFY OK: task exists, state=$($task.State), last result=$($info.LastTaskResult)."

    $triggers = (Get-ScheduledTask -TaskName $TaskName).Triggers
    Write-InstallLog "VERIFY OK: $($triggers.Count) trigger(s) configured."

    $action = (Get-ScheduledTask -TaskName $TaskName).Actions[0]
    if ($action.Execute -ne "powershell.exe" -or $action.Arguments -notmatch [regex]::Escape($ApplyScript)) {
        Write-InstallLog "VERIFY FAIL: task action does not point to apply_fn_lock.ps1."
        return $false
    }

    Write-InstallLog "VERIFY OK: task action points to apply_fn_lock.ps1."
    return $true
}

function Test-HidppReady {
    $python = Get-Command python, python3, py -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $python) {
        return $false
    }

    & $python.Source $ToolPath --probe 2>$null
    return $LASTEXITCODE -eq 0
}

Write-InstallLog "=== K400+ Fn Lock autostart setup ==="

if ($Uninstall) {
    Remove-AutostartTask
    exit 0
}

if (-not (Test-Prerequisites)) {
    exit 1
}

Remove-AutostartTask

try {
    Install-AutostartTask -DelaySeconds $LogonDelaySeconds
} catch {
    Write-InstallLog "ERROR: failed to register task: $($_.Exception.Message)"
    exit 2
}

if (-not (Test-AutostartTask)) {
    exit 3
}

if ($SkipTestRun) {
    Write-InstallLog "Skipped live test run (-SkipTestRun)."
    Write-InstallLog "Done. Task '$TaskName' is installed."
    exit 0
}

if (-not (Test-HidppReady)) {
    Write-InstallLog "Live test skipped: Logitech HID++ receiver not detected right now."
    Write-InstallLog "Done. Task '$TaskName' is installed; it will run after next logon when the dongle is available."
    exit 0
}

Write-InstallLog "Running live test (HID++ interface detected)..."
& $ApplyScript
$testExit = $LASTEXITCODE

if ($testExit -eq 0) {
    Write-InstallLog "Live test OK: Fn Lock applied (exit 0)."
    Write-InstallLog "Done. Task '$TaskName' is installed and verified."
    exit 0
}

Write-InstallLog "Live test FAIL: apply_fn_lock.ps1 exit code $testExit."
Write-InstallLog "Task is installed but Fn Lock could not be applied now. Check $LogDir\apply.log"
exit 4
