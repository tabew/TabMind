@echo off
title QQBot 守护进程 (防意外关闭与自动重启)
cd /d "%~dp0"

echo ========================================================
echo        QQBot 24小时无人值守守护进程已启动
echo ========================================================
echo.

:loop
echo [%date% %time%] 正在启动 TabMind.exe...
TabMind.exe
echo.
echo [警告] 检测到 TabMind.exe 退出或关闭！
echo [%date% %time%] 将在 3 秒后自动重新启动机器人进程...
timeout /t 3 /nobreak >nul
goto loop