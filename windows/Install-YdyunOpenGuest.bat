@echo off
setlocal
cd /d "%~dp0"

fltmc >nul 2>&1
if errorlevel 1 goto request_admin

echo [INFO] Installing YDYUN open-source Windows Guest components...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0resources\Install-YdyunOpenGuest.ps1" %*
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" goto install_failed
echo [OK] Installation finished. Reboot Windows when prompted.
goto finish

:request_admin
echo [INFO] Requesting administrator privileges...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
exit /b 0

:install_failed
echo [ERROR] Installation failed. Exit code: %RC%

:finish
echo.
pause
exit /b %RC%
