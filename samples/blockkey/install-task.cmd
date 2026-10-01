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

REM Administrator rights are required.  Started without them and without options,
REM this asks for them the usual way: the UAC prompt appears and the elevated
REM copy runs in its own window, which cmd /k keeps open for the messages.
REM Options are not forwarded through the UAC boundary on purpose: cmd re-parses
REM expanded arguments and would mangle anything containing & or quotes.
whoami /groups | findstr /c:"S-1-16-12288" >nul
if errorlevel 1 (
    if not "%~1"=="" (
        echo install-task.cmd: with options, open a command prompt started as
        echo                   administrator and run this there, so nothing gets
        echo                   mangled on the way
        popd
        exit /b 1
    )
    echo install-task.cmd: asking for administrator rights
    powershell -NoProfile -Command "Start-Process -FilePath cmd.exe -ArgumentList '/k','\"%~f0\"' -Verb RunAs" 2>nul
    if errorlevel 1 echo install-task.cmd: administrator rights were refused, nothing was changed
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
