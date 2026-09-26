@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
if not exist "LanMessenger.exe" goto missing
start "" "%~dp0LanMessenger.exe"
exit /b 0

:missing
echo [局域网安全通讯] 找不到 LanMessenger.exe
echo 请先把整个压缩包解压到一个文件夹里再运行（不要在压缩包里直接双击）。
echo 当前目录：%~dp0
pause
exit /b 1
