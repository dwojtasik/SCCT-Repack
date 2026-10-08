@echo off
setlocal EnableExtensions
cd /d "%~dp0"

if not exist ".venv\Scripts\pyinstaller.exe" (
    echo Run setup_venv.bat first.
    exit /b 1
)

set "PYI=.venv\Scripts\pyinstaller.exe"
set "DIST=dist"
set "WORK=build"

echo Building onefile EXEs ...
"%PYI%" --noconfirm --clean --onefile --console --name unpack --distpath "%DIST%" --workpath "%WORK%" --specpath "%WORK%" unpack.py
if errorlevel 1 exit /b 1
"%PYI%" --noconfirm --clean --onefile --console --name pack --distpath "%DIST%" --workpath "%WORK%" --specpath "%WORK%" pack.py
if errorlevel 1 exit /b 1
"%PYI%" --noconfirm --clean --onefile --console --name patch_exe --distpath "%DIST%" --workpath "%WORK%" --specpath "%WORK%" patch_exe.py
if errorlevel 1 exit /b 1

echo.
echo Built:
echo   %DIST%\unpack.exe
echo   %DIST%\pack.exe
echo   %DIST%\patch_exe.exe
exit /b 0
