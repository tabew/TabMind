#ifndef _WIN32
// Linux / POSIX 实现：服务器无 GUI，日志只走 stdout + logs/app.log。
// 额外做了按大小轮转，避免长期运行把 40G 系统盘写满。

#include "platform.hpp"

#include <filesystem>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <sys/stat.h>

namespace {

constexpr long kLogRotateBytes = 8L * 1024 * 1024;  // 单文件上限 8MB

// 调用方需持有日志锁
void rotate_if_needed(const char* path, const char* bak) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    if (ec) return;                       // 文件不存在，无需轮转
    if (sz < static_cast<uintmax_t>(kLogRotateBytes)) return;
    std::filesystem::rename(path, bak, ec);   // 覆盖旧的 .1
}

}  // namespace

void file_log(const std::string& msg) {
    static std::mutex mtx;
    std::lock_guard<std::mutex> lk(mtx);
    try {
        std::error_code ec;
        std::filesystem::create_directories("logs", ec);

        rotate_if_needed("logs/app.log", "logs/app.log.1");

        std::time_t tt = std::time(nullptr);
        std::tm tm{};
        platform::localtime_safe(tm, tt);

        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);

        // 纯 UTF-8，不加 BOM：Linux 下 tail/grep 直接可读
        FILE* f = fopen("logs/app.log", "ab");
        if (f) {
            fprintf(f, "[%s] %s\n", stamp, msg.c_str());
            fclose(f);
        }
    } catch (...) {}
}

namespace platform {

void log(const std::string& msg) {
    // std::endl 会 flush，保证 systemd journal 里实时可见
    std::cout << msg << std::endl;
    file_log(msg);
}

void localtime_safe(std::tm& out, std::time_t t) {
    localtime_r(&t, &out);
}

void ensure_dir(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
}

}  // namespace platform

#endif  // !_WIN32
