@echo off
REM Alternative to install-service.cmd: run blockkey as a scheduled task at
REM every boot.  No service and no extra log file, but the same effect.
REM
REM Run this from a command prompt started as administrator.  Any options given
REM to this script are passed to blockkey at every start:
REM
REM     install-task.cmd
REM     install-task.cmd --hardware-id "VID_2717&PID_5011&REV_0100&MI_00"

setlocal
pushd "%~dp0"

set "TASK=blockkey"
set "EXE=%~dp0blockkey.exe"

whoami /groups | findstr /c:"S-1-16-12288" >nul
if errorlevel 1 (
    echo install-task.cmd: run this from a command prompt started as administrator
    popd
    exit /b 1
)

if not exist "%EXE%" (
    echo install-task.cmd: blockkey.exe is missing here, run build-msvc.cmd first
    popd
    exit /b 1
)

schtasks /create /tn "%TASK%" /sc onstart /ru SYSTEM /rl HIGHEST /f /tr "\"%EXE%\" --quiet %*"
if errorlevel 1 (
    echo install-task.cmd: could not create the scheduled task
    popd
    exit /b 1
)

schtasks /run /tn "%TASK%" >nul

echo.
echo blockkey runs as the scheduled task "%TASK%" at every boot.
echo Stop it:   schtasks /end /tn %TASK%
echo Start it:  schtasks /run /tn %TASK%
echo Remove it: uninstall-task.cmd
popd
exit /b 0
