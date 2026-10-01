@echo off
REM Stop and remove the blockkey scheduled task installed by install-task.cmd.

setlocal
pushd "%~dp0"

set "TASK=blockkey"

REM Administrator rights are required.  Started without them this asks for them
REM the usual way: the UAC prompt appears and the elevated copy runs in its own
REM window, which cmd /k keeps open for the messages.
whoami /groups | findstr /c:"S-1-16-12288" >nul
if errorlevel 1 (
    echo uninstall-task.cmd: asking for administrator rights
    powershell -NoProfile -Command "Start-Process -FilePath cmd.exe -ArgumentList '/k','\"%~f0\"' -Verb RunAs" 2>nul
    if errorlevel 1 echo uninstall-task.cmd: administrator rights were refused, nothing was changed
    exit /b 1
)

schtasks /end /tn "%TASK%" >nul 2>nul

schtasks /delete /tn "%TASK%" /f
if errorlevel 1 (
    echo uninstall-task.cmd: could not remove the scheduled task
    popd
    exit /b 1
)

echo.
echo the scheduled task "%TASK%" was removed; the key is no longer blocked
popd
exit /b 0
