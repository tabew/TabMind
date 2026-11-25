#pragma once
#include <string>
#include <ctime>

// ---------------------------------------------------------------------------
// 平台抽象层
//
// Windows：GUI 主程序，日志转发到 MainWindow 的日志面板
// Linux  ：服务器无头运行（无 X11 / 无 GUI），日志只走 stdout + logs/app.log
//
// 核心业务代码（ai / message / plugin / core）只依赖本头文件，不再直接碰 UI。
// ---------------------------------------------------------------------------

// 仅写文件日志 logs/app.log（线程安全，UTF-8）
// Windows 下该符号原本定义在 ui_core.cpp，移植后统一由 platform_*.cpp 提供
void file_log(const std::string& msg);

namespace platform {

// 写运行日志：stdout + logs/app.log（Windows 额外推送到 UI 日志面板）
void log(const std::string& msg);

// 跨平台的 localtime（Windows: localtime_s / POSIX: localtime_r）
void localtime_safe(std::tm& out, std::time_t t);

// 幂等创建目录（已存在不报错）
void ensure_dir(const std::string& path);

}  // namespace platform
