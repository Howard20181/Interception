@echo off
REM Stop and remove the blockkey service installed by install-service.cmd, and
REM delete the copy of the program that script placed in the standard location.
REM
REM Run this from a command prompt started as administrator.

setlocal
set "SERVICE=blockkey"

REM Administrator rights are required.  Started without them this asks for them
REM the usual way: the UAC prompt appears and the elevated copy runs in its own
REM window, which cmd /k keeps open for the messages.  Options are not forwarded
REM through the UAC boundary on purpose: cmd re-parses expanded arguments and
REM would mangle anything containing & or quotes.
whoami /groups | findstr /c:"S-1-16-12288" >nul
if errorlevel 1 (
    if not "%~1"=="" (
        echo uninstall-service.cmd: with options, open a command prompt started as
        echo                        administrator and run this there
        exit /b 1
    )
    echo uninstall-service.cmd: asking for administrator rights
    powershell -NoProfile -Command "Start-Process -FilePath cmd.exe -ArgumentList '/k','\"%~f0\"' -Verb RunAs" 2>nul
    if errorlevel 1 echo uninstall-service.cmd: administrator rights were refused, nothing was changed
    exit /b 1
)

REM The service remembers the path it runs, which is where the files are.
set "IMAGE="
for /f "tokens=2,*" %%a in ('reg query "HKLM\SYSTEM\CurrentControlSet\Services\%SERVICE%" /v ImagePath 2^>nul ^| findstr /i "ImagePath"') do set "IMAGE=%%b"

sc query "%SERVICE%" >nul 2>nul
if errorlevel 1 (
    echo uninstall-service.cmd: the service is not installed
) else (
    sc stop "%SERVICE%" >nul
    echo waiting for the service to stop
    ping -n 3 127.0.0.1 >nul

    sc delete "%SERVICE%"
    if errorlevel 1 (
        echo uninstall-service.cmd: could not remove the service
        exit /b 1
    )
)

reg delete "HKLM\SYSTEM\CurrentControlSet\Services\EventLog\Application\%SERVICE%" /f >nul 2>nul

REM Take the folder from the service command line, for instance
REM "C:\Program Files\blockkey\blockkey.exe" --service
set "DIR="
if defined IMAGE for %%i in (%IMAGE%) do if not defined DIR set "DIR=%%~dpi"
if not defined DIR (
    set "DEFAULT=%ProgramW6432%"
    if not defined DEFAULT set "DEFAULT=%ProgramFiles%"
    set "DIR=%DEFAULT%\blockkey\"
)

if /i "%DIR%"=="%~dp0" (
    REM Installed in place, by hand or by an older version: leave the files.
    echo the program files in "%DIR%" were left alone
) else if exist "%DIR%blockkey.exe" (
    echo removing the program files from "%DIR%"
    del /q "%DIR%blockkey.exe" "%DIR%interception.dll" "%DIR%README.txt" 2>nul
    REM Not recursive: the folder only goes away when nothing else is in it.
    rd "%DIR%" 2>nul
)

echo.
echo the service "%SERVICE%" was removed; the key is no longer blocked
exit /b 0
