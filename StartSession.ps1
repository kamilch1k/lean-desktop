# Runs outside the packaged app via Task Scheduler, then exits; no resident PowerShell.
$ErrorActionPreference = 'Stop'
$installDir = $PSScriptRoot
$exe = Join-Path $installDir 'LeanBar.exe'
$companion = Join-Path $installDir 'QuickSearch.Companion.exe'
$state = Get-Content -LiteralPath (Join-Path $installDir 'install-state.json') -Raw | ConvertFrom-Json
$session = (Get-Process -Id $PID).SessionId
try {
    if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $companion)) { throw 'The replacement executable is missing. Explorer was left running.' }
    if (Get-Process LeanBar -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $session -and $_.Path -eq $exe }) {
        if (-not (Get-Process 'QuickSearch.Companion' -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $session -and $_.Path -eq $companion })) { Start-Process -FilePath $companion -WindowStyle Hidden }
        exit 0
    }
    # The real startup folder is handled here, outside MSIX file virtualization.
    $backup = Join-Path $installDir 'QuickSearch-startup.backup.lnk'
    if ($state.HadStartupLink -and (Test-Path -LiteralPath $state.StartupLink)) {
        if (-not (Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $state.StartupLink -Destination $backup }
        Remove-Item -LiteralPath $state.StartupLink
    }
    $ws = New-Object -ComObject WScript.Shell
    $link = $ws.CreateShortcut($state.RestoreLink)
    $link.TargetPath = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $link.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $installDir 'Restore.ps1') + '"'
    $link.WorkingDirectory = $installDir
    $link.Description = 'Restore Explorer desktop and taskbar; remove LeanBar sign-in startup'
    $link.Save()
    $startLink = $ws.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'Start Lean Desktop.lnk'))
    $startLink.TargetPath = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $startLink.Arguments = '-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "' + (Join-Path $installDir 'StartSession.ps1') + '"'
    $startLink.WorkingDirectory = $installDir
    $startLink.WindowStyle = 7
    $startLink.IconLocation = (Join-Path $env:SystemRoot 'System32\shell32.dll') + ',34'
    $startLink.Description = 'Start Lean Desktop and taskbar, then stop Windows Explorer'
    $startLink.Save()
    Get-Process QuickSearch -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $session -and $_.Path -eq $state.OriginalSearch } | Stop-Process
    $native = Start-Process -FilePath $exe -ArgumentList '--takeover' -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 3
    if ($native.HasExited) { throw ('LeanBar exited during startup: ' + $native.ExitCode) }
    [pscustomobject]@{At=(Get-Date).ToString('o');Pid=$native.Id;Status='Started';StartupLinkRemoved=(-not (Test-Path -LiteralPath $state.StartupLink))} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $installDir 'session-start.json') -Encoding UTF8
} catch {
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $installDir 'startup-error.txt') -Encoding UTF8
    if (-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -eq $session)) { Start-Process explorer.exe -WindowStyle Hidden }
    if ((Test-Path -LiteralPath $state.OriginalSearch) -and -not (Get-Process QuickSearch -ErrorAction SilentlyContinue | Where-Object SessionId -eq $session)) { Start-Process -FilePath $state.OriginalSearch -WindowStyle Hidden }
    exit 1
}
