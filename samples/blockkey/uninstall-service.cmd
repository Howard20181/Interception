@echo off
REM Stop and remove the blockkey service installed by install-service.cmd.

setlocal
pushd "%~dp0"

set "SERVICE=blockkey"

whoami /groups | findstr /c:"S-1-16-12288" >nul
if errorlevel 1 (
    echo uninstall-service.cmd: run this from a command prompt started as administrator
    popd
    exit /b 1
)

sc query "%SERVICE%" >nul 2>nul
if errorlevel 1 (
    echo uninstall-service.cmd: the service is not installed
    popd
    exit /b 0
)

sc stop "%SERVICE%" >nul
echo waiting for the service to stop
ping -n 3 127.0.0.1 >nul

sc delete "%SERVICE%"
if errorlevel 1 (
    echo uninstall-service.cmd: could not remove the service
    popd
    exit /b 1
)

echo.
echo the service "%SERVICE%" was removed; the key is no longer blocked
popd
exit /b 0
