@echo off
setlocal
cd /d "%~dp0"
if not exist "LanMessenger.exe" goto missing
start "" "%~dp0LanMessenger.exe"
exit /b 0

:missing
echo [LAN Secure Messenger] LanMessenger.exe not found next to this script.
echo Please extract the WHOLE zip file to a folder first (do not run it from inside the zip).
echo Folder: %~dp0
pause
exit /b 1
