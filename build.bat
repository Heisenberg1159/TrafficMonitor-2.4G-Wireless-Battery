@echo off
rem Builds WirelessBattery.dll (x64 Release) with Visual Studio's bundled CMake + Ninja.
rem Usage:
rem   build.bat                         build only
rem   build.bat install <TrafficMonitor dir>   build, then copy the DLL into <dir>\plugins
rem                                     (TrafficMonitor must not be running)
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VSROOT (
    if exist "%VSWHERE%" (
        for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
    )
)
if not defined VSROOT (
    echo Visual Studio with the C++ workload was not found. Set VSROOT to its install folder.
    exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set CMAKE="%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

cd /d "%~dp0"
%CMAKE% -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
%CMAKE% --build build || exit /b 1

if /i "%~1"=="install" (
    if "%~2"=="" (
        echo Usage: build.bat install ^<TrafficMonitor folder^>
        exit /b 1
    )
    copy /y build\WirelessBattery.dll "%~2\plugins\" || exit /b 1
    echo Installed to %~2\plugins
)
endlocal
