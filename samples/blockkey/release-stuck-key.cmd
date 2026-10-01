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
REM Needs the Interception driver; administrator rights are not normally needed,
REM but if the driver cannot be reached, right click this file and run it as
REM administrator.

setlocal
pushd "%~dp0"

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
    echo release-stuck-key.cmd: that did not work.  If it mentions the driver or
    echo                        administrator rights, right click this file and
    echo                        run it as administrator.
)

echo.
echo done.  If the modifier still looks stuck, tap it on the other side of the
echo keyboard once, or press Ctrl+Alt+Del.
echo.
echo the window closes in 20 seconds, or press any key now
timeout /t 20 >nul

popd
exit /b 0
