#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#endif

#include "config.hpp"
#include "bot_client.hpp"
#include "ui_window.hpp"

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 设置全局代码页为 UTF-8，避免中文乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    MainWindow& win = MainWindow::get_instance();
    if (win.create_and_show(hInstance, nCmdShow)) {
        win.run_message_loop();
    }

    return 0;
}
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOW);
#else
    std::cout << "GUI Window currently requires Windows." << std::endl;
    return 0;
#endif
}
