$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
Push-Location $repo
try {
    & .\build.cmd
    if ($LASTEXITCODE -ne 0) { throw 'Application build failed.' }
    & .\tests\build.cmd
    if ($LASTEXITCODE -ne 0) { throw 'Test build failed.' }
    $fixtures = Join-Path $repo 'build\fixtures'
    $bin = Join-Path $repo 'build\bin'
    New-Item -ItemType Directory -Force (Join-Path $fixtures 'Projects') | Out-Null
    Set-Content (Join-Path $fixtures 'Projects\example.txt') 'Example project file'
    Set-Content (Join-Path $fixtures 'Notes.txt') 'Example notes'
    Set-Content (Join-Path $fixtures 'Readme.txt') 'Example document'
    Set-Content (Join-Path $fixtures 'hidden.txt') 'Hidden test fixture'
    (Get-Item (Join-Path $fixtures 'hidden.txt') -Force).Attributes = 'Hidden'
    Set-Content (Join-Path $fixtures 'Launch.url') "[InternetShortcut]`r`nURL=https://example.com`r`nIconFile=$env:WINDIR\System32\shell32.dll`r`nIconIndex=14"
    $ws = New-Object -ComObject WScript.Shell
    $link = $ws.CreateShortcut((Join-Path $fixtures 'Notepad.lnk'))
    $link.TargetPath = Join-Path $env:WINDIR 'System32\notepad.exe'
    $link.IconLocation = (Join-Path $env:WINDIR 'System32\notepad.exe') + ',0'
    $link.Save()
    $self = Start-Process -FilePath (Join-Path $bin 'LeanBar.exe') -ArgumentList '--selftest' -WindowStyle Hidden -Wait -PassThru
    if ($self.ExitCode -ne 0) { throw 'Taskbar self-test failed.' }
    & .\build\tests\NativeTests.exe $bin $fixtures
    if ($LASTEXITCODE -ne 0) { throw 'Native integration tests failed.' }
    & .\build\tests\RenderSwitcher.exe (Join-Path $bin 'QuickSearch.Companion.exe') (Join-Path $bin 'alt-tab-preview.png')
    if ($LASTEXITCODE -ne 0) { throw 'Switcher rendering failed.' }
    & .\build\tests\InputLanguage.exe (Join-Path $bin 'QuickSearch.Companion.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Input-language switching failed.' }
    Add-Type -AssemblyName System.Drawing
    foreach ($panel in 'sound','bluetooth','network') {
        $render = Join-Path $bin ($panel + '-preview.png')
        $p = Start-Process -FilePath (Join-Path $bin 'LeanControls.exe') -ArgumentList ('--demo --' + $panel + ' --render "' + $render + '"') -WindowStyle Hidden -Wait -PassThru
        if ($p.ExitCode -ne 0) { throw "Controls render failed: $panel" }
        $bitmap = [Drawing.Bitmap]::new($render)
        try {
            $colors = [Collections.Generic.HashSet[int]]::new()
            for ($x=0; $x -lt $bitmap.Width; $x+=13) { for ($y=0; $y -lt $bitmap.Height; $y+=13) { [void]$colors.Add($bitmap.GetPixel($x,$y).ToArgb()) } }
            if ($colors.Count -lt 8) { throw "Controls render is blank: $panel" }
        } finally { $bitmap.Dispose() }
    }
    foreach ($file in 'Install.ps1','Restore.ps1','StartSession.ps1') {
        $tokens=$null; $errors=$null
        [void][System.Management.Automation.Language.Parser]::ParseFile((Join-Path $repo $file),[ref]$tokens,[ref]$errors)
        if ($errors) { throw ($errors | Out-String) }
    }
    'All tests passed. Fixture renders are in build\bin.'
} finally { Pop-Location }
