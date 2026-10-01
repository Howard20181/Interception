@echo off
REM Install the Interception driver, which blockkey cannot work without.
REM
REM Run this from a command prompt started as administrator, with the official
REM installer next to this script (the "command line installer" folder of the
REM Interception release).  Its full path can also be given as an argument:
REM
REM     install-driver.cmd
REM     install-driver.cmd "D:\somewhere\install-interception.exe"
REM
REM The installer writes keyboard.sys and mouse.sys into
REM %SystemRoot%\System32\drivers and registers them as class filters.  Windows
REM only starts filtering after a restart, which the installer says as well.

setlocal
pushd "%~dp0"

set "INSTALLER=%~dp0install-interception.exe"
if not exist "%INSTALLER%" if exist "%~1" set "INSTALLER=%~1"

REM Administrator rights are required.  Started without them and without options,
REM this asks for them the usual way: the UAC prompt appears and the elevated
REM copy runs in its own window, which cmd /k keeps open for the messages.
REM
REM With an option (the path of the installer here), arguments are deliberately
REM not forwarded through the UAC boundary: cmd re-parses expanded arguments and
REM would mangle anything containing & or quotes.  Use an elevated prompt then.
whoami /groups | findstr /c:"S-1-16-12288" >nul
if errorlevel 1 (
    if not "%~1"=="" (
        echo install-driver.cmd: run this from a command prompt started as administrator,
        echo                    with the option, so it is not mangled on the way
        popd
        exit /b 1
    )
    echo install-driver.cmd: asking for administrator rights
    powershell -NoProfile -Command "Start-Process -FilePath cmd.exe -ArgumentList '/k','\"%~f0\"' -Verb RunAs" 2>nul
    if errorlevel 1 echo install-driver.cmd: administrator rights were refused, nothing was changed
    popd
    exit /b 1
)

if not exist "%INSTALLER%" (
    echo install-driver.cmd: install-interception.exe was not found next to this script.
    echo                    Take it from the Interception release, from its "command
    echo                    line installer" folder, or pass the full path:
    echo                        install-driver.cmd "D:\path\install-interception.exe"
    popd
    exit /b 1
)

sc query keyboard >nul 2>nul
if not errorlevel 1 echo install-driver.cmd: the driver looks installed already, installing it again anyway

echo running "%INSTALLER%" /install
"%INSTALLER%" /install
if errorlevel 1 (
    echo install-driver.cmd: the installer reported a failure
    popd
    exit /b 1
)

REM --------------------------------------------- what the installer left behind
set "DRIVERS=%SystemRoot%\System32\drivers"
set "MISSING="

if not exist "%DRIVERS%\keyboard.sys" set "MISSING=%MISSING% keyboard.sys"
if not exist "%DRIVERS%\mouse.sys" set "MISSING=%MISSING% mouse.sys"

reg query "HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e96b-e325-11ce-bfc1-08002be10318}" /v UpperFilters 2>nul | findstr /i "keyboard" >nul
if errorlevel 1 set "MISSING=%MISSING% keyboard-class-filter"

reg query "HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e96f-e325-11ce-bfc1-08002be10318}" /v UpperFilters 2>nul | findstr /i "mouse" >nul
if errorlevel 1 set "MISSING=%MISSING% mouse-class-filter"

echo.
if defined MISSING (
    echo install-driver.cmd: installed, but these did not show up:%MISSING%
    echo                    have a look at "%DRIVERS%" and the keyboard/mouse
    echo                    class filters in the registry
) else (
    echo the driver is in place: keyboard.sys, mouse.sys and both class filters
)

echo.
echo RESTART Windows now: the filter drivers only take effect after a reboot.
echo Afterwards blockkey.exe --list should list your keyboards.
echo     shutdown /r /t 0        (if you want to restart from here)
popd
exit /b 0
