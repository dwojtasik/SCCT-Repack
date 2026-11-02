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
rem --noupx: UPX-packed EXEs are flagged by many antivirus engines
set "OPTS=--noconfirm --clean --onefile --console --noupx --distpath %DIST% --workpath %WORK% --specpath %WORK%"

echo Writing version resources ...
".venv\Scripts\python.exe" make_version_files.py "%WORK%"
if errorlevel 1 exit /b 1

echo Building onefile EXEs ...
"%PYI%" %OPTS% --name unpack --version-file "%~dp0%WORK%\version_unpack.txt" unpack.py
if errorlevel 1 exit /b 1
"%PYI%" %OPTS% --name pack --version-file "%~dp0%WORK%\version_pack.txt" pack.py
if errorlevel 1 exit /b 1
"%PYI%" %OPTS% --name patch_exe --version-file "%~dp0%WORK%\version_patch_exe.txt" patch_exe.py
if errorlevel 1 exit /b 1
"%PYI%" %OPTS% --name remaster_patch_exe --version-file "%~dp0%WORK%\version_remaster_patch_exe.txt" --add-data "%~dp0scct\data\Remaster.dll;scct\data" remaster_patch_exe.py
if errorlevel 1 exit /b 1

echo.
echo Built:
echo   %DIST%\unpack.exe
echo   %DIST%\pack.exe
echo   %DIST%\patch_exe.exe
echo   %DIST%\remaster_patch_exe.exe
exit /b 0
