@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
rem 从不自动修改防火墙：未启用时只提示（由 lan-daemon 检测）；已启用时询问（默认：否）。
echo 将为 lan-daemon.exe 添加入站规则（TCP 41235 / UDP 41234，专用/域网络），需要管理员确认：
echo   lan-daemon.exe firewall-setup
set A=
set /p A=是否继续？[y/N]: 
if /i not "%A%"=="y" if /i not "%A%"=="yes" (
  echo 未修改防火墙。
  goto end
)
"%~dp0lan-daemon.exe" firewall-setup
echo.
"%~dp0lan-daemon.exe" firewall-status
:end
echo.
echo 提醒：云服务器 / 虚拟机还需在服务商控制台的安全组中放行 TCP 41235 和 UDP 41234。
pause
