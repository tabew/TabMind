#pragma once
#include <windows.h>
#include <string>
#include <memory>

// ---------------------------------------------------------------------------
// TabMind 前端（v4 全自绘）
//
// 架构变更：不再使用"嵌入式子窗口卡片 + 原生控件"。
// 整个界面（侧栏、卡片、按钮、输入框、下拉、复选、日志）全部由主窗口
// 用一个 HDC 自绘，进程里没有第二个 HWND。
//
// 这样彻底消除了此前所有同类问题的根源：
//   - 子窗口鼠标捕获泄漏（卡片吸住全系统鼠标输入 → 界面像死机）
//   - 拖动状态残留（drag_mode_ 忘记复位 → 鼠标一碰卡片就乱跑）
//   - z 序重排、控件隐藏/恢复、坐标被卡片自身移动抵消
//   - WM_CANCELMODE / WM_CAPTURECHANGED 的多窗口路由歧义
// 拖动和悬停都只是"改数据 + 重画一小块"，不存在跨窗口状态。
//
// 对外接口保持不变（get_instance / create_and_show / run_message_loop / log），
// 因此 main.cpp 与 bot_client.cpp 无需改动。
// ---------------------------------------------------------------------------
class MainWindow {
public:
    static MainWindow& get_instance();

    bool create_and_show(HINSTANCE hInstance, int nCmdShow);
    void run_message_loop();

    // 线程安全：可从任意线程调用，内部转投到 UI 线程
    void log(const std::string& message);

    ~MainWindow();

private:
    MainWindow();
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    struct Impl;
    std::unique_ptr<Impl> d_;
};
