#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <filesystem>

#include "config.hpp"
#include "bot_client.hpp"
#include "platform.hpp"

#ifdef _WIN32
#include <windows.h>
#include "ui_window.hpp"
#else
#include "plugin_manager.hpp"
#endif

#ifdef _WIN32
// ---------------------------------------------------------------------------
// Windows：GUI 入口（单窗口全自绘）
// ---------------------------------------------------------------------------
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

int main(int argc, char* argv[]) {
    return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOW);
}

#else
// ---------------------------------------------------------------------------
// Linux：无头（headless）入口
//
// 服务器没有图形环境，整个 UI 层不参与编译。启动流程与 GUI 版一致：
//   读 config.json → 加载插件 → 连 OneBot WebSocket → 常驻运行
// 由 systemd 托管，日志走 stdout（journalctl）与 logs/app.log。
// ---------------------------------------------------------------------------
namespace {

std::atomic<bool> g_running{true};

void handle_signal(int sig) {
    (void)sig;
    g_running = false;
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    // SIGTERM/SIGINT：systemd stop 或 Ctrl+C 时优雅退出
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGINT, handle_signal);
    // 往已断开的 socket 写数据时不要被 SIGPIPE 打死进程
    std::signal(SIGPIPE, SIG_IGN);

    platform::log("========================================");
    platform::log("  TabMind headless 启动 (Linux)");
    platform::log("========================================");

    // 工作目录检查：config.json 必须和可执行文件在同一工作目录
    if (!std::filesystem::exists("config.json")) {
        platform::log("[启动] 未找到 config.json，将按默认配置生成一份");
    }
    AppConfig config = AppConfig::load_from_file("config.json");

    // 必要的运行目录
    platform::ensure_dir("data");
    platform::ensure_dir("logs");
    platform::ensure_dir("plugins");

    // 关键配置自检：把容易踩的坑直接打在日志里
    if (config.ai.api_key.empty()) {
        platform::log("[配置] ⚠️ ai.api_key 为空，AI 回复会全部失败（401）。"
                      "请编辑 config.json 填入真实密钥后重启。");
    }
    if (config.onebot.bot_qq == 0) {
        platform::log("[配置] ⚠️ onebot.bot_qq 为 0，@ 判定可能不准确。");
    }
    platform::log("[配置] AI  base_url=" + config.ai.base_url +
                  "  model=" + config.ai.model);
    platform::log("[配置] OneBot ws_url=" + config.onebot.ws_url +
                  "  bot_qq=" + std::to_string(config.onebot.bot_qq));

    // 加载 C++ 插件（Linux 下只认 .so，Windows 的 .dll 无法加载）
    PluginManager::get_instance().load_plugins("plugins");

    BotClient bot(config);
    bot.start();

    platform::log("[启动] 已进入运行状态，等待消息。收到 SIGTERM 后退出。");

    // 主循环：只负责等待退出信号，业务都在 WebSocket 回调线程里
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    platform::log("[退出] 收到退出信号，正在停止...");
    bot.stop();
    PluginManager::get_instance().unload_all();
    platform::log("[退出] 已停止。");

    return 0;
}

#endif  // _WIN32
