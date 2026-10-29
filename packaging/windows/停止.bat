@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
echo 正在停止后台服务 lan-daemon ...
"%~dp0lan-msg.exe" shutdown
if errorlevel 1 taskkill /im lan-daemon.exe /f >nul 2>&1
taskkill /im LanMessenger.exe >nul 2>&1
echo 完成。
timeout /t 2 >nul
