@echo off
pushd %~dp0
echo ============================================
echo   Bolt Engine Setup
echo ============================================
echo.
set "PYTHON_VERSION="
python --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python is not installed or not in PATH.
    echo         Download from https://www.python.org/downloads/
    PAUSE
    exit /b 1
)
for /f "delims=" %%i in ('python --version 2^>^&1') do set "PYTHON_VERSION=%%i"
python -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Bolt setup requires Python 3.10 or newer.
    if defined PYTHON_VERSION echo         Found %PYTHON_VERSION%.
    echo         Download a newer version from https://www.python.org/downloads/
    PAUSE
    exit /b 1
)
python Setup.py %*
PAUSE
