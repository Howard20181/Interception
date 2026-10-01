@echo off
REM Install blockkey as a Windows service, from a copy placed in a standard
REM location, so the running program does not depend on this folder.
REM
REM Run this from a command prompt started as administrator.  Any option that is
REM not meant for this script is stored in the service and passed to blockkey at
REM every start:
REM
REM     install-service.cmd
REM     install-service.cmd --hardware-id "VID_2717&PID_5011&REV_0100&MI_00"
REM
REM Only blockkey.exe, interception.dll (when present) and README.txt are copied
REM to %ProgramFiles%\blockkey.  Set BLOCKKEY_INSTALL_DIR to install elsewhere,
REM without a trailing backslash:
REM
REM     set BLOCKKEY_INSTALL_DIR=D:\tools\blockkey

setlocal
pushd "%~dp0"

set "SERVICE=blockkey"
set "SOURCE=%~dp0"

REM Where the running copy goes: the 64 bit Program Files even when this
REM script is run from a 32 bit command prompt.
set "INSTALL_DIR=%ProgramW6432%"
if not defined INSTALL_DIR set "INSTALL_DIR=%ProgramFiles%"
set "INSTALL_DIR=%INSTALL_DIR%\blockkey"
if defined BLOCKKEY_INSTALL_DIR set "INSTALL_DIR=%BLOCKKEY_INSTALL_DIR%"
set "TARGET=%INSTALL_DIR%\blockkey.exe"

REM Administrator rights are required.  Started without them and without options,
REM this asks for them the usual way: the UAC prompt appears and the elevated
REM copy runs in its own window, which cmd /k keeps open for the messages.
REM
REM Options are deliberately not forwarded through the UAC boundary, nor is
REM BLOCKKEY_INSTALL_DIR honoured there: cmd re-parses expanded arguments and
REM would mangle anything containing & or quotes, and a custom install directory
REM would silently fall back to the default.  Use an elevated prompt instead.
whoami /groups | findstr /c:"S-1-16-12288" >nul
if errorlevel 1 (
    if not "%~1"=="" (
        echo install-service.cmd: with options, open a command prompt started as
        echo                      administrator and run this there, so nothing gets
        echo                      mangled on the way
        popd
        exit /b 1
    )
    if defined BLOCKKEY_INSTALL_DIR (
        echo install-service.cmd: with BLOCKKEY_INSTALL_DIR, open a command prompt
        echo                      started as administrator and run this there
        popd
        exit /b 1
    )
    echo install-service.cmd: asking for administrator rights
    powershell -NoProfile -Command "Start-Process -FilePath cmd.exe -ArgumentList '/k','\"%~f0\"' -Verb RunAs" 2>nul
    if errorlevel 1 echo install-service.cmd: administrator rights were refused, nothing was changed
    popd
    exit /b 1
)

if not exist "%SOURCE%blockkey.exe" (
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

REM ---------------------------------------------------------------- copy --
echo installing to "%INSTALL_DIR%"

if not exist "%INSTALL_DIR%" (
    md "%INSTALL_DIR%"
    if errorlevel 1 (
        echo install-service.cmd: could not create "%INSTALL_DIR%"
        popd
        exit /b 1
    )
)

copy /y "%SOURCE%blockkey.exe" "%INSTALL_DIR%\" >nul
if errorlevel 1 (
    echo install-service.cmd: could not copy blockkey.exe to "%INSTALL_DIR%"
    popd
    exit /b 1
)

REM A statically linked build has no interception.dll; the dynamic one needs it.
if exist "%SOURCE%interception.dll" copy /y "%SOURCE%interception.dll" "%INSTALL_DIR%\" >nul
if exist "%SOURCE%README.txt" copy /y "%SOURCE%README.txt" "%INSTALL_DIR%\" >nul

if not exist "%TARGET%" (
    echo install-service.cmd: "%TARGET%" is still missing after the copy
    popd
    exit /b 1
)

REM ------------------------------------------------------------- service --
sc create "%SERVICE%" binPath= "\"%TARGET%\" --service %*" start= auto DisplayName= "blockkey key blocker"
if errorlevel 1 (
    echo install-service.cmd: could not create the service
    popd
    exit /b 1
)

sc description "%SERVICE%" "Swallows the configured keyboard key, for instance a broken right Alt, on the selected keyboards."
sc failure "%SERVICE%" reset= 0 actions= restart/5000/restart/5000/restart/5000 >nul

REM Register the event log source, pointing at the installed copy: it carries
REM the message table built from blockkey.mc, so the Event Viewer shows readable
REM text.  Windows then owns the log size (Application log, 20 MB by default).
reg add "HKLM\SYSTEM\CurrentControlSet\Services\EventLog\Application\%SERVICE%" /v EventMessageFile /t REG_EXPAND_SZ /d "%TARGET%" /f >nul
reg add "HKLM\SYSTEM\CurrentControlSet\Services\EventLog\Application\%SERVICE%" /v TypesSupported /t REG_DWORD /d 7 /f >nul

sc start "%SERVICE%"
if errorlevel 1 (
    echo.
    echo install-service.cmd: the service did not start, look for source "%SERVICE%"
    echo                       in the Application log of the Event Viewer
    popd
    exit /b 1
)

echo.
echo blockkey is installed in "%INSTALL_DIR%" as the service "%SERVICE%".
echo It starts at every boot, before anybody logs in.
echo Events:     Event Viewer ^> Windows Logs ^> Application, source "%SERVICE%"
echo             ^(or: Get-WinEvent -ProviderName %SERVICE%^)
echo Stop it:    sc stop %SERVICE%
echo Remove it:  uninstall-service.cmd
popd
exit /b 0
