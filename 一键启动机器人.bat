@echo off
title QQBot 生产环境一键启动助手
cd /d "%~dp0"

echo ========================================================
echo          永雏塔菲 QQ 机器人 (Windows Server)
echo ========================================================
echo.

:: 1. 检查 NapCat 目录
if not exist "NapCat\launcher.bat" (
    echo [错误] 未检测到 NapCat 目录，请确保完整解压了部署包！
    pause
    exit /b
)

:: 2. 启动 NapCat
echo [1/2] 正在后台启动 NapCat OneBot 核心服务...
start "NapCat-QQ服务" /d "%~dp0NapCat" cmd /c "launcher.bat"

:: 等待 NapCat 启动 WebSocket 服务
echo 正在等待 NapCat 初始化 (5 秒)...
timeout /t 5 /nobreak >nul

:: 3. 启动 QQBotAI 客户端
echo [2/2] 正在启动 QQBotAI 主程序...
start "" "%~dp0QQBotAI.exe"

echo.
echo ========================================================
echo  [√] 启动完成！
echo  - 请在 NapCat 窗口中根据提示进行 QQ 扫码或快速登录。
echo  - QQBotAI 图形管理面板已打开，保持窗口运行即可 24h 挂机。
echo ========================================================
echo.
pause