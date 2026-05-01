@echo off
pushd %~dp0
echo ============================================
echo   Axiom Engine Setup
echo ============================================
echo.
set "PYTHON_VERSION="
set "PYTHON="

py -3.10 --version >nul 2>&1
if not errorlevel 1 set "PYTHON=py -3.10"

if not defined PYTHON (
    py -3 --version >nul 2>&1
    if not errorlevel 1 set "PYTHON=py -3"
)

if not defined PYTHON (
    python --version >nul 2>&1
    if not errorlevel 1 set "PYTHON=python"
)

if not defined PYTHON (
    echo [ERROR] Python is not installed or not in PATH.
    echo         Download from https://www.python.org/downloads/
    PAUSE
    exit /b 1
)
for /f "delims=" %%i in ('%PYTHON% --version 2^>^&1') do set "PYTHON_VERSION=%%i"
%PYTHON% -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Axiom setup requires Python 3.10 or newer.
    if defined PYTHON_VERSION echo         Found %PYTHON_VERSION%.
    echo         Download a newer version from https://www.python.org/downloads/
    PAUSE
    exit /b 1
)
%PYTHON% Setup.py %*
PAUSE
