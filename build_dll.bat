@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Builds scct\data\Remaster.dll (32-bit) from remaster\ with MinGW-w64 i686 g++ and windres.
rem Set CXX / WINDRES to the tools if they are not on PATH under these names.
if "%CXX%"=="" set "CXX=i686-w64-mingw32-g++"
if "%WINDRES%"=="" set "WINDRES=i686-w64-mingw32-windres"
if not exist build mkdir build

echo Building Remaster.dll ...
"%WINDRES%" remaster\remaster.rc -O coff -o build\remaster.res
if errorlevel 1 goto failed
"%CXX%" -O2 -std=c++17 -Wall -shared -static -s -o scct\data\Remaster.dll remaster\remaster.cpp build\remaster.res remaster\remaster.def -lmsvfw32 -lmsacm32 -lgdi32 -luser32 -Wl,--kill-at,--dynamicbase,--nxcompat
if errorlevel 1 goto failed

echo.
echo Built:
echo   scct\data\Remaster.dll
exit /b 0

:failed
echo Build failed. Needs MinGW-w64 i686 g++ and windres ^(e.g. MSYS2 mingw-w64-i686-gcc^); set CXX / WINDRES if they are not on PATH.
exit /b 1
