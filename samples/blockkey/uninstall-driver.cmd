@echo off
REM Remove the Interception driver installed by install-driver.cmd.
REM
REM Run this from a command prompt started as administrator, with the official
REM installer next to this script.  A restart is needed for the removal to take
REM effect, and the broken key goes back to misbehaving after it.

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
        echo uninstall-driver.cmd: run this from a command prompt started as administrator,
        echo                      with the option, so it is not mangled on the way
        popd
        exit /b 1
    )
    echo uninstall-driver.cmd: asking for administrator rights
    powershell -NoProfile -Command "Start-Process -FilePath cmd.exe -ArgumentList '/k','\"%~f0\"' -Verb RunAs" 2>nul
    if errorlevel 1 echo uninstall-driver.cmd: administrator rights were refused, nothing was changed
    popd
    exit /b 1
)

if not exist "%INSTALLER%" (
    echo uninstall-driver.cmd: install-interception.exe was not found next to this script.
    echo                      Take it from the Interception release, or pass its path:
    echo                          uninstall-driver.cmd "D:\path\install-interception.exe"
    popd
    exit /b 1
)

if not exist "%SystemRoot%\System32\drivers\keyboard.sys" (
    echo uninstall-driver.cmd: the driver does not look installed
    popd
    exit /b 0
)

sc query blockkey >nul 2>nul
if not errorlevel 1 (
    echo uninstall-driver.cmd: the blockkey service is still installed; remove it first
    echo                        with uninstall-service.cmd, or it will keep failing to
    echo                        reach the driver
)

echo running "%INSTALLER%" /uninstall
"%INSTALLER%" /uninstall
if errorlevel 1 (
    echo uninstall-driver.cmd: the installer reported a failure
    popd
    exit /b 1
)

echo.
echo RESTART Windows now for the removal to take effect.
echo Keep blockkey stopped afterwards, or the broken key misbehaves again.
popd
exit /b 0
