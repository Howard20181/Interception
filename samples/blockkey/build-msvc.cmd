@echo off
REM Build blockkey.exe with Visual Studio, as a 64 bit program.
REM
REM The Interception library is compiled into the executable, so nothing but the
REM installed driver is needed to run it.  For a WDK based build see sources,
REM makefile and buildit.cmd instead.

setlocal
pushd "%~dp0"

if defined VCINSTALLDIR goto :build

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_vs

set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto :no_vs

call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
if errorlevel 1 goto :fail

:build
REM The event log message table is nice to have, not required: without it the
REM service still journals, only without a readable description.  mc.exe and
REM rc.exe come with the Windows SDK; building touches no log either way.
set "RES="
mc -h . -r . blockkey.mc >nul 2>nul
if not errorlevel 1 rc /nologo /fo blockkey.res blockkey.rc >nul 2>nul
if exist blockkey.res set "RES=blockkey.res"
if not defined RES echo build-msvc.cmd: mc.exe/rc.exe not available, building without the event log message table

cl /nologo /O2 /W3 /EHsc /DINTERCEPTION_STATIC /D_CRT_SECURE_NO_WARNINGS ^
   /I..\..\library /I.. ^
   blockkey.cpp ..\utils.c ..\..\library\interception.c %RES% ^
   /Fe:blockkey.exe /link user32.lib advapi32.lib
if errorlevel 1 goto :fail

echo.
echo built %CD%\blockkey.exe
popd
exit /b 0

:no_vs
echo build-msvc.cmd: Visual Studio was not found, use the WDK scripts or any
echo                other C++ compiler instead, see README.md.
popd
exit /b 1

:fail
echo build-msvc.cmd: build failed
popd
exit /b 1
