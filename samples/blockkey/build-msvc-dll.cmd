@echo off
REM Build blockkey-dll.exe by linking the released interception.lib, instead of
REM compiling library\interception.c into the executable.
REM
REM The executable then needs interception.dll next to it, which this script
REM copies for you.
REM
REM     build-msvc-dll.cmd          x64, from library\x64, gives blockkey-dll.exe
REM     build-msvc-dll.cmd x86      x86, from library\x86, gives blockkey-dll-x86.exe

setlocal
pushd "%~dp0"

set "ARCH=x64"
set "OUT=blockkey-dll.exe"
set "LIBDIR=..\..\library\x64"
if /i "%~1"=="x86" set "ARCH=x86"
if /i "%~1"=="x86" set "OUT=blockkey-dll-x86.exe"
if /i "%~1"=="x86" set "LIBDIR=..\..\library\x86"

if not exist "%LIBDIR%\interception.lib" (
    echo build-msvc-dll.cmd: %LIBDIR%\interception.lib is missing.
    echo                   Extract the released libraries there first, or use build-msvc.cmd.
    popd
    exit /b 1
)

if defined VCINSTALLDIR goto :build

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_vs

set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto :no_vs

call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=%ARCH% -no_logo
if errorlevel 1 goto :fail

:build
REM The event log message table is nice to have, not required: without it the
REM service still journals, only without a readable description.  mc.exe and
REM rc.exe come with the Windows SDK; building touches no log either way.
set "RES="
mc -h . -r . blockkey.mc >nul 2>nul
if not errorlevel 1 rc /nologo /fo blockkey.res blockkey.rc >nul 2>nul
if exist blockkey.res set "RES=blockkey.res"
if not defined RES echo build-msvc-dll.cmd: mc.exe/rc.exe not available, building without the event log message table

cl /nologo /O2 /W3 /EHsc /D_CRT_SECURE_NO_WARNINGS ^
   /I ..\..\library /I .. ^
   blockkey.cpp ..\utils.c %RES% ^
   /Fe:%OUT% /link "%LIBDIR%\interception.lib" user32.lib advapi32.lib
if errorlevel 1 goto :fail

copy /y "%LIBDIR%\interception.dll" . >nul

echo.
echo built %CD%\%OUT%  (%ARCH%, needs the interception.dll copied next to it)
popd
exit /b 0

:no_vs
echo build-msvc-dll.cmd: Visual Studio was not found; use the WDK scripts or any
echo                   other C++ compiler instead, see README.md.
popd
exit /b 1

:fail
echo build-msvc-dll.cmd: build failed
popd
exit /b 1
