#ifdef _WIN32
// Windows 实现：日志既写文件，也推送到 GUI 日志面板。
// 本文件只在 MSVC 构建时参与编译，不影响 Linux 目标。

#include "platform.hpp"
#include "ui_window.hpp"

#include <windows.h>
#include <filesystem>
#include <cstdio>
#include <iostream>
#include <mutex>

void file_log(const std::string& msg) {
    static std::mutex mtx;
    std::lock_guard<std::mutex> lk(mtx);
    try {
        std::error_code ec;
        std::filesystem::create_directories("logs", ec);

        std::time_t tt = std::time(nullptr);
        std::tm tm{};
        platform::localtime_safe(tm, tt);

        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);

        // 用二进制模式写，自己保证 UTF-8：
        // 文本模式 "a" 在 Windows 上是 ANSI 编码，中文会写成乱码，
        // 服务器上排查问题全靠这个文件，乱码等于没有日志。
        bool is_new = !std::filesystem::exists("logs/app.log", ec);
        FILE* f = nullptr;
        if (fopen_s(&f, "logs/app.log", "ab") == 0 && f) {
            if (is_new) fwrite("\xEF\xBB\xBF", 1, 3, f);   // UTF-8 BOM
            fprintf(f, "[%s] %s\n", stamp, msg.c_str());
            fclose(f);
        }
    } catch (...) {}
}

namespace platform {

void log(const std::string& msg) {
    std::cout << msg << std::endl;
    file_log(msg);
    MainWindow::get_instance().log(msg);
}

void localtime_safe(std::tm& out, std::time_t t) {
    localtime_s(&out, &t);
}

void ensure_dir(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
}

}  // namespace platform

#endif  // _WIN32
