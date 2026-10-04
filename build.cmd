@echo off
setlocal
pushd "%~dp0"
where cl.exe >nul 2>nul
if errorlevel 1 (
  if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    echo Install Visual Studio Build Tools with Desktop development with C++.
    exit /b 1
  )
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
)
if not exist build\bin mkdir build\bin
cl /nologo /O2 /W4 /std:c++17 /EHsc /MT /D_WIN32_WINNT=0x0A00 LeanBar.cpp /Fo:build\LeanBar.obj /Fe:build\bin\LeanBar.exe /link /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT user32.lib gdi32.lib shell32.lib dwmapi.lib advapi32.lib ole32.lib comctl32.lib uuid.lib
if errorlevel 1 exit /b 1
cl /nologo /O2 /W4 /std:c++17 /EHsc /MT /D_WIN32_WINNT=0x0A00 LeanControls.cpp /Fo:build\LeanControls.obj /Fe:build\bin\LeanControls.exe /link /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT user32.lib gdi32.lib shell32.lib ole32.lib comctl32.lib uuid.lib windowsapp.lib wlanapi.lib iphlpapi.lib propsys.lib gdiplus.lib ksuser.lib version.lib
if errorlevel 1 exit /b 1
"%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:winexe /out:build\bin\QuickSearch.Companion.exe QuickSearch.Companion.cs
if errorlevel 1 exit /b 1
for %%f in (LeanBar.ini Install.ps1 Restore.ps1 StartSession.ps1 README.md) do copy /y "%%f" build\bin\ >nul
copy /y "Restore Windows.cmd" build\bin\ >nul
copy /y "Restore taskbar.cmd" build\bin\ >nul
copy /y "Preview desktop.cmd" build\bin\ >nul
copy /y "Start replacement.cmd" build\bin\ >nul
echo Built build\bin\LeanBar.exe and QuickSearch.Companion.exe
popd
exit /b 0
