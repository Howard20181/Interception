@echo off
REM Emergency release for a key that a broken keyboard left stuck.
REM
REM Windows then believes the modifier is still held, which turns Enter in a
REM console window into Alt+Enter: the window goes fullscreen instead of running
REM the command.  This script first shows a few seconds of what the keyboard is
REM sending right now (--probe), then sends the key up (--release-only).
REM
REM     release-stuck-key.cmd           five seconds of probing
REM     release-stuck-key.cmd 10        ten seconds instead
REM
REM It asks for administrator rights itself: a running blockkey has restricted
REM the driver's devices to administrators, so an unelevated run could not reach
REM them.  Started with an argument, run it from an elevated command prompt
REM instead, since options are not forwarded through the UAC prompt.

setlocal
pushd "%~dp0"

REM ------------------------------------------------- administrator rights --
whoami /groups | findstr /c:"S-1-16-12288" >nul
if errorlevel 1 (
    if not "%~1"=="" (
        echo release-stuck-key.cmd: with an argument, open a command prompt started
        echo                         as administrator and run this there
        popd
        exit /b 1
    )
    echo release-stuck-key.cmd: asking for administrator rights
    powershell -NoProfile -Command "Start-Process -FilePath cmd.exe -ArgumentList '/k','\"%~f0\"' -Verb RunAs" 2>nul
    if errorlevel 1 echo release-stuck-key.cmd: administrator rights were refused, nothing was done
    popd
    exit /b 1
)

set "SECONDS=%~1"
if "%SECONDS%"=="" set "SECONDS=5"
echo %SECONDS%| findstr /r "^[1-9][0-9]*$" >nul
if errorlevel 1 set "SECONDS=5"

if not exist "%~dp0blockkey.exe" (
    echo release-stuck-key.cmd: blockkey.exe is missing here
    popd
    exit /b 1
)

echo release-stuck-key.cmd: showing %SECONDS% seconds of what the keyboard sends
echo (press the stuck key, or just wait; every stroke is printed below)
echo.

powershell -NoProfile -Command "$p = Start-Process -FilePath '%~dp0blockkey.exe' -ArgumentList '--probe' -PassThru -NoNewWindow; for ($i = 0; $i -lt %SECONDS%; $i++) { if ($p.HasExited) { break }; Start-Sleep -Seconds 1 }; if (-not $p.HasExited) { $p.Kill() }"

echo.
echo release-stuck-key.cmd: sending the key up now
"%~dp0blockkey.exe" --release-only
if errorlevel 1 (
    echo.
    echo release-stuck-key.cmd: that did not work.  The most likely reasons are
    echo                        that blockkey.exe is missing next to this script,
    echo                        or that the Interception driver is not installed.
)

echo.
echo done.  If the modifier still looks stuck, tap it on the other side of the
echo keyboard once, or press Ctrl+Alt+Del.
echo.
echo the window closes in 20 seconds, or press any key now
timeout /t 20 >nul

popd
exit /b 0
