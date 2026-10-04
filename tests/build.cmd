@echo off
setlocal
pushd "%~dp0.."
where cl.exe >nul 2>nul
if errorlevel 1 (
  for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
)
if not exist build\tests mkdir build\tests
cl /nologo /utf-8 /O2 /W4 /wd4459 /std:c++17 /EHsc /MT /D_WIN32_WINNT=0x0A00 tests\NativeTests.cpp /Fo:build\NativeTests.obj /Fe:build\tests\NativeTests.exe /link user32.lib gdi32.lib shell32.lib dwmapi.lib advapi32.lib ole32.lib comctl32.lib uuid.lib gdiplus.lib
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /O2 /W4 /std:c++17 /EHsc /MT /D_WIN32_WINNT=0x0A00 tests\AppTests.cpp /Fo:build\AppTests.obj /Fe:build\tests\AppTests.exe /link user32.lib advapi32.lib dwmapi.lib psapi.lib
if errorlevel 1 exit /b 1
"%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:exe /out:build\tests\RenderSwitcher.exe tests\RenderSwitcher.cs
if errorlevel 1 exit /b 1
"%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:exe /out:build\tests\InputLanguage.exe tests\InputLanguage.cs
if errorlevel 1 exit /b 1
popd
exit /b 0
