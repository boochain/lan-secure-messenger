@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo 正在启动局域网加密通讯...
echo 首次运行会自动生成密钥；请允许防火墙访问专用网络。
start "" "%~dp0lan-gui.exe"
