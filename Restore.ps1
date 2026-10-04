$ErrorActionPreference = 'Stop'
$installDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'LeanBar'
$statePath = Join-Path $installDir 'install-state.json'
if (-not (Test-Path -LiteralPath $statePath)) { throw 'LeanBar install state was not found.' }
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
Unregister-ScheduledTask -TaskName $state.TaskName -Confirm:$false -ErrorAction SilentlyContinue
$exe = Join-Path $installDir 'LeanBar.exe'
Start-Process -FilePath $exe -ArgumentList '--restore' -WindowStyle Hidden -Wait
Start-Sleep -Milliseconds 700
$session = (Get-Process -Id $PID).SessionId
Get-Process 'QuickSearch.Companion' -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $session -and $_.Path -eq (Join-Path $installDir 'QuickSearch.Companion.exe') } | Stop-Process
$backup = Join-Path $installDir 'QuickSearch-startup.backup.lnk'
if ((Test-Path -LiteralPath $backup) -and -not (Test-Path -LiteralPath $state.StartupLink)) { Move-Item -LiteralPath $backup -Destination $state.StartupLink }
if (-not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -eq $session)) { Start-Process explorer.exe -WindowStyle Hidden }
if ((Test-Path -LiteralPath $state.OriginalSearch) -and -not (Get-Process QuickSearch -ErrorAction SilentlyContinue | Where-Object SessionId -eq $session)) { Start-Process -FilePath $state.OriginalSearch -WindowStyle Hidden }
'Windows desktop and taskbar restored. LeanBar will not start at the next sign-in. Its files and backups are retained.'
