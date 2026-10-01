@echo off
REM Stop and remove the blockkey scheduled task installed by install-task.cmd.

setlocal
pushd "%~dp0"

set "TASK=blockkey"

whoami /groups | findstr /c:"S-1-16-12288" >nul
if errorlevel 1 (
    echo uninstall-task.cmd: run this from a command prompt started as administrator
    popd
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
