@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Builds scct\data\Remaster.dll (32-bit) from remaster\*.cpp with MinGW-w64 i686 g++.
rem Set CXX to the compiler if it is not on PATH as i686-w64-mingw32-g++.
if "%CXX%"=="" set "CXX=i686-w64-mingw32-g++"

echo Building Remaster.dll ...
"%CXX%" -O2 -std=c++17 -Wall -shared -static -s -o scct\data\Remaster.dll remaster\remaster.cpp -Wl,--kill-at
if errorlevel 1 (
    echo Build failed. Needs MinGW-w64 i686 g++ ^(e.g. MSYS2 mingw-w64-i686-gcc^); set CXX if it is not on PATH.
    exit /b 1
)

echo.
echo Built:
echo   scct\data\Remaster.dll
exit /b 0
