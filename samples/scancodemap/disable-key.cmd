@echo off
REM Disable one keyboard key with the Scancode Map that Windows itself applies in
REM kbdclass.sys: no driver, no signing, and no device that other users could
REM open (unlike an interception driver).
REM
REM     disable-key.cmd                  disable right Alt (scan code E0 38)
REM     disable-key.cmd 3A               disable another key, without E0 prefix
REM     disable-key.cmd E052             disable another key, with E0 prefix
REM     disable-key.cmd /show            show the current setting, change nothing
REM     disable-key.cmd /restore         put back what was there before
REM     disable-key.cmd /replace E052    overwrite an existing Scancode Map
REM
REM It writes
REM     HKLM\SYSTEM\CurrentControlSet\Control\Keyboard Layout\Scancode Map
REM and only takes effect after a reboot.
REM
REM The byte layout, checked against SharpKeys and the worked examples in the
REM Emacs FAQ for Windows: eight zero bytes, then the number of DWORDs including
REM the trailing null, then one DWORD per mapping as
REM     target low, target high, source low, source high
REM and finally a zero DWORD.  "Turn key off" is a target of 0000.

setlocal
pushd "%~dp0"

set "KEY=HKLM\SYSTEM\CurrentControlSet\Control\Keyboard Layout"
set "VALUE=Scancode Map"
set "BACKUP=%~dp0scancode-map-backup.reg"
set "DECODE=$k='HKLM:\SYSTEM\CurrentControlSet\Control\Keyboard Layout'; $v=(Get-ItemProperty -Path $k -Name 'Scancode Map' -ErrorAction SilentlyContinue).'Scancode Map'; if ($null -eq $v) { '  (not set, so no key is remapped)' } else { $n=[BitConverter]::ToUInt32($v,8); for ($i=0; $i -lt $n-1; $i++) { $d=[BitConverter]::ToUInt32($v,12+4*$i); '  {0:X4} -> {1:X4}' -f ($d -shr 16), ($d -band 0xFFFF) } }"

REM ------------------------------------------------- administrator rights --
whoami /groups | findstr /c:"S-1-16-12288" >nul
if errorlevel 1 (
    if not "%~1"=="" (
        echo disable-key.cmd: with an option, open a command prompt started as
        echo                  administrator and run this there
        popd
        exit /b 1
    )
    echo disable-key.cmd: asking for administrator rights
    powershell -NoProfile -Command "Start-Process -FilePath cmd.exe -ArgumentList '/k','\"%~f0\"' -Verb RunAs" 2>nul
    if errorlevel 1 echo disable-key.cmd: administrator rights were refused, nothing was changed
    popd
    exit /b 1
)

REM ------------------------------------------------------------ arguments --
set "ACTION=disable"
set "SCAN=E038"
if /i "%~1"=="/show" set "ACTION=show"
if /i "%~1"=="/restore" set "ACTION=restore"
if /i "%~1"=="/replace" set "ACTION=replace"
if /i "%ACTION%"=="disable" if not "%~1"=="" set "SCAN=%~1"
if /i "%ACTION%"=="show" if not "%~2"=="" set "SCAN=%~2"
if /i "%ACTION%"=="replace" if not "%~2"=="" set "SCAN=%~2"

set "VALID="
echo %SCAN%| findstr /r /i "^[0-9a-f][0-9a-f]$" >nul && set "VALID=1"
echo %SCAN%| findstr /r /i "^[0-9a-f][0-9a-f][0-9a-f][0-9a-f]$" >nul && set "VALID=1"
if not defined VALID (
    echo disable-key.cmd: "%SCAN%" is not a scan code such as 3A or E038
    popd
    exit /b 1
)

REM Two digit scan codes get the 00 high byte, four digit ones carry E0 already.
set "SRC=%SCAN%"
if "%SCAN:~2,1%"=="" set "SRC=00%SCAN%"
set "MAP=0000%SRC:~2,2%%SRC:~0,2%"
set "DATA=000000000000000002000000%MAP%00000000"

if /i "%ACTION%"=="show" goto :show
if /i "%ACTION%"=="restore" goto :restore

REM ------------------------------------------------- do not clobber silently --
reg query "%KEY%" /v "%VALUE%" >nul 2>nul
if not errorlevel 1 if /i not "%ACTION%"=="replace" goto :refuse

REM ----------------------------------------------------------------- write --
if not exist "%BACKUP%" (
    reg export "%KEY%" "%BACKUP%" /y >nul 2>nul
    if exist "%BACKUP%" echo saved the previous setting to "%BACKUP%"
)

echo writing Scancode Map = %DATA%
reg add "%KEY%" /v "%VALUE%" /t REG_BINARY /d %DATA% /f
if errorlevel 1 (
    echo disable-key.cmd: could not write the value
    popd
    exit /b 1
)

echo.
echo done: scan code %SCAN% now maps to nothing, so that key is dead.
echo REBOOT for it to take effect ^(it is read when the keyboard starts^).
echo To undo: disable-key.cmd /restore
popd
exit /b 0

REM ------------------------------------------------------------------ show --
:show
echo current Scancode Map:
powershell -NoProfile -Command "%DECODE%"
echo.
echo this script would write for %SCAN%:
echo   %DATA%
echo   meaning: scan code %SCAN% becomes 0000 ^(turned off^)
popd
exit /b 0

REM --------------------------------------------------------------- restore --
:restore
if exist "%BACKUP%" (
    echo restoring "%BACKUP%"
    reg import "%BACKUP%"
) else (
    echo no backup in this folder, removing the value instead
    reg delete "%KEY%" /v "%VALUE%" /f >nul 2>nul
)
echo.
echo REBOOT for the change to take effect
popd
exit /b 0

REM ---------------------------------------------------------------- refuse --
:refuse
echo disable-key.cmd: this machine already has a Scancode Map:
powershell -NoProfile -Command "%DECODE%"
echo.
echo   Not overwriting it.  Either add the mapping with SharpKeys, which
echo   merges, or rerun this as:  disable-key.cmd /replace %SCAN%
popd
exit /b 1
