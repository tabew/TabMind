@echo off
REM Build TabMind (Release x64) with MSVC command line.
REM Paths are machine-local; adjust MSVC/SDK if Visual Studio is elsewhere.
setlocal

set MSVC=D:\vs studio\a\VC\Tools\MSVC\14.44.35207
set SDK=D:\Windows Kits\10
set SDKV=10.0.26100.0

set INCLUDE=%MSVC%\include;%SDK%\Include\%SDKV%\ucrt;%SDK%\Include\%SDKV%\um;%SDK%\Include\%SDKV%\shared
set LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKV%\ucrt\x64;%SDK%\Lib\%SDKV%\um\x64

if not exist obj mkdir obj
if not exist bin\Release mkdir bin\Release

"%MSVC%\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /O2 /MT /EHsc /utf-8 ^
 /D NDEBUG /D _WINDOWS /D NOMINMAX /D _CRT_SECURE_NO_WARNINGS /D UNICODE /D _UNICODE ^
 /I include /Foobj\ /Fe:bin\Release\TabMind.exe ^
 src\ai\ai_client.cpp src\core\bot_client.cpp src\core\main.cpp ^
 src\message\message_handler.cpp src\plugin\plugin_manager.cpp ^
 src\ui\ui_dialogs.cpp src\ui\ui_core.cpp src\ui\ui_window.cpp ^
 /link /SUBSYSTEM:WINDOWS ^
 user32.lib gdi32.lib shell32.lib ole32.lib comdlg32.lib advapi32.lib imm32.lib ^
 winhttp.lib ws2_32.lib crypt32.lib comctl32.lib uxtheme.lib dwmapi.lib msimg32.lib

if %errorlevel%==0 (echo BUILD OK: bin\Release\TabMind.exe) else (echo BUILD FAILED)
endlocal
