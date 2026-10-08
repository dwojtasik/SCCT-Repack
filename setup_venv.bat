@echo off
setlocal EnableExtensions
cd /d "%~dp0"

where python >nul 2>&1
if errorlevel 1 (
    echo Python is not on PATH. Install Python 3.10+ and retry.
    exit /b 1
)

if not exist ".venv\Scripts\python.exe" (
    echo Creating virtualenv .venv ...
    python -m venv .venv
    if errorlevel 1 exit /b 1
)

echo Installing build dependencies ...
".venv\Scripts\python.exe" -m pip install --upgrade pip
if errorlevel 1 exit /b 1
".venv\Scripts\python.exe" -m pip install -r requirements.txt -r build_requirements.txt
if errorlevel 1 exit /b 1

echo.
echo venv ready. Use build.bat to produce unpack.exe, pack.exe, and patch_exe.exe.
exit /b 0
