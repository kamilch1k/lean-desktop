param([switch]$StartNow)
$ErrorActionPreference = 'Stop'
$taskName = ("LeanBar Desktop - " + $env:USERNAME)
$installDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'LeanBar'
$statePath = Join-Path $installDir 'install-state.json'
$originalSearch = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'QuickSearch\QuickSearch.exe'
$startupLink = Join-Path ([Environment]::GetFolderPath('Startup')) 'QuickSearch.lnk'
$restoreLink = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Restore Windows.lnk'
$shellSetting = Get-ItemPropertyValue 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon' 'AutoRestartShell'
if ($shellSetting -ne 0) { throw 'Explorer restart is enabled. No changes made. This installer is tailored to the existing shell-test configuration (AutoRestartShell=0).' }
foreach ($name in 'LeanBar.exe','LeanControls.exe','QuickSearch.Companion.exe','LeanBar.ini','Restore.ps1','StartSession.ps1','README.md') {
    if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $name))) { throw "Missing package file: $name" }
}
$existingTask = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($existingTask -and $existingTask.Description -ne 'LeanBar native desktop and taskbar; Explorer fallback; installed by LeanBar.') { throw 'A different task already uses this name.' }
New-Item -ItemType Directory -Path $installDir -Force | Out-Null
if (-not (Test-Path -LiteralPath $statePath)) {
    if (Test-Path -LiteralPath $restoreLink) { throw 'Restore Windows.lnk already exists. No shortcut was overwritten.' }
    $state = [ordered]@{ OriginalSearch=$originalSearch; StartupLink=$startupLink; HadStartupLink=(Test-Path -LiteralPath $startupLink); RestoreLink=$restoreLink; AutoRestartShellBefore=$shellSetting; TaskName=$taskName; InstalledAt=(Get-Date).ToString('o') }
    $state | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
}
foreach ($name in 'LeanBar.exe','LeanControls.exe','QuickSearch.Companion.exe','LeanBar.ini','Restore.ps1','StartSession.ps1','README.md') {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $installDir $name) -Force
}
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$backup = Join-Path $installDir 'QuickSearch-startup.backup.lnk'
try {
    if ($state.HadStartupLink -and (Test-Path -LiteralPath $startupLink) -and -not (Test-Path -LiteralPath $backup)) {
        Move-Item -LiteralPath $startupLink -Destination $backup
    }
    $user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
    $action = New-ScheduledTaskAction -Execute (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') -Argument ('-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "' + (Join-Path $installDir 'StartSession.ps1') + '"') -WorkingDirectory $installDir
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $user
    $trigger.Delay = 'PT8S'
    $principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew -Priority 4
    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Description 'LeanBar native desktop and taskbar; Explorer fallback; installed by LeanBar.' -Force | Out-Null
    Export-ScheduledTask -TaskName $taskName | Set-Content -LiteralPath (Join-Path $installDir 'startup-task.xml') -Encoding UTF8
    $ws = New-Object -ComObject WScript.Shell
    $link = $ws.CreateShortcut($restoreLink)
    $link.TargetPath = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $link.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $installDir 'Restore.ps1') + '"'
    $link.WorkingDirectory = $installDir
    $link.Description = 'Restore Explorer desktop and taskbar; remove LeanBar sign-in startup'
    $link.Save()
    if ($StartNow) {
        $session = (Get-Process -Id $PID).SessionId
        Get-Process QuickSearch -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $session -and $_.Path -eq $originalSearch } | Stop-Process
        Start-ScheduledTask -TaskName $taskName
    }
    [pscustomobject]@{Installed=$true;Directory=$installDir;Task=$taskName;Started=[bool]$StartNow;RestoreShortcut=$restoreLink} | ConvertTo-Json
} catch {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    if ((Test-Path -LiteralPath $backup) -and -not (Test-Path -LiteralPath $startupLink)) { Move-Item -LiteralPath $backup -Destination $startupLink }
    throw
}
