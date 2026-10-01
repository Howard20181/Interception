@echo off
REM Install blockkey as a Windows service, so the key stays blocked from boot
REM on, before anybody logs in.
REM
REM Run this from a command prompt started as administrator.  Any options given
REM to this script are stored in the service and passed to it at every start:
REM
REM     install-service.cmd
REM     install-service.cmd --hardware-id "VID_2717&PID_5011&REV_0100&MI_00"

setlocal
pushd "%~dp0"

set "SERVICE=blockkey"
set "EXE=%~dp0blockkey.exe"
set "LOG=%ProgramData%\blockkey\blockkey.log"

whoami /groups | findstr /c:"S-1-16-12288" >nul
if errorlevel 1 (
    echo install-service.cmd: run this from a command prompt started as administrator
    popd
    exit /b 1
)

if not exist "%EXE%" (
    echo install-service.cmd: blockkey.exe is missing here, run build-msvc.cmd first
    popd
    exit /b 1
)

sc query "%SERVICE%" >nul 2>nul
if not errorlevel 1 (
    echo install-service.cmd: the service is already installed, run uninstall-service.cmd first
    popd
    exit /b 1
)

sc create "%SERVICE%" binPath= "\"%EXE%\" --service %*" start= auto DisplayName= "blockkey key blocker"
if errorlevel 1 (
    echo install-service.cmd: could not create the service
    popd
    exit /b 1
)

sc description "%SERVICE%" "Swallows the configured keyboard key, for instance a broken right Alt, on the selected keyboards."
sc failure "%SERVICE%" reset= 0 actions= restart/5000/restart/5000/restart/5000 >nul

sc start "%SERVICE%"
if errorlevel 1 (
    echo.
    echo install-service.cmd: the service did not start, see "%LOG%"
    popd
    exit /b 1
)

echo.
echo blockkey is installed as the service "%SERVICE%" and starts at every boot.
echo Log file:  "%LOG%"
echo Stop it:   sc stop %SERVICE%
echo Remove it: uninstall-service.cmd
popd
exit /b 0
