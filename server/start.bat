@echo off
chcp 65001 >nul
cd /d "%~dp0"

set PORT=5000

echo ============================================
echo   Smart Call System - Patient Server
echo   URL: http://LAN-IP:%PORT%
echo ============================================
echo.

echo [1/2] Checking port %PORT% ...
set OLD_PID=

for /f "tokens=5" %%a in ('netstat -ano ^| findstr /c:":%PORT% " ^| findstr LISTENING') do set OLD_PID=%%a

if defined OLD_PID (
    echo       Found process PID=%OLD_PID%
    tasklist /fi "PID eq %OLD_PID%" 2>nul | findstr /i "python" >nul
    if not errorlevel 1 (
        echo       Killing old python process...
        taskkill /F /PID %OLD_PID% >nul 2>&1
        ping 127.0.0.1 -n 2 >nul
        echo       Done.
    ) else (
        echo       WARNING: process is not python, skip.
        echo       Please close the app using port %PORT% manually.
        pause
        exit /b 1
    )
) else (
    echo       Port is free.
)

echo.
echo [2/2] Starting server...
echo.
python app.py

pause
