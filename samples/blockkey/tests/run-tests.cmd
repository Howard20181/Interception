@echo off
REM Build and run the host side test of blockkey.
REM
REM No driver, no administrative rights and no Interception binaries are needed:
REM the test stubs the Interception API itself.  Visual Studio, clang and gcc
REM are all accepted.
REM
REM This script deliberately uses no GOTO label, so that it also works when the
REM file is checked out with LF line endings.

setlocal
pushd "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VCINSTALLDIR if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VCINSTALLDIR if defined VSPATH call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo

set "COMPILER="
where cl >nul 2>nul && set "COMPILER=cl"
if not defined COMPILER where g++ >nul 2>nul && set "COMPILER=g++"
if not defined COMPILER where clang++ >nul 2>nul && set "COMPILER=clang++"

if not defined COMPILER (
    echo run-tests.cmd: no C++ compiler was found in PATH
    popd
    exit /b 1
)

echo building the host side test with %COMPILER%
if "%COMPILER%"=="cl" cl /nologo /W3 /EHsc /D_CRT_SECURE_NO_WARNINGS /I..\.. /I..\..\..\library blockkey_test.cpp /Fe:blockkey_test.exe /link advapi32.lib ntdll.lib
if "%COMPILER%"=="g++" g++ -std=c++03 -Wall -Wextra -I..\.. -I..\..\..\library blockkey_test.cpp -o blockkey_test.exe -ladvapi32 -lntdll
if "%COMPILER%"=="clang++" clang++ -std=c++03 -Wall -Wextra -I..\.. -I..\..\..\library blockkey_test.cpp -o blockkey_test.exe -ladvapi32 -lntdll

if errorlevel 1 (
    echo run-tests.cmd: the test did not build
    popd
    exit /b 1
)

blockkey_test.exe
set "RESULT=%ERRORLEVEL%"

echo.
if "%RESULT%"=="0" (echo blockkey host tests passed) else (echo blockkey host tests FAILED)

popd
exit /b %RESULT%
