@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
echo 正在配置 Windows 防火墙（会弹出一次管理员确认）...
"%~dp0lan-daemon.exe" firewall-setup
echo.
"%~dp0lan-daemon.exe" firewall-status
echo.
pause
