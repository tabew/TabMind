#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include "config.hpp"
#include "bot_client.hpp"
#include "plugin_manager.hpp"

#define WM_USER_LOG (WM_USER + 101)
#define WM_USER_STATUS_UPDATE (WM_USER + 102)

class MainWindow {
public:
    static MainWindow& get_instance();

    bool create_and_show(HINSTANCE hInstance, int nCmdShow);
    void run_message_loop();
    void log(const std::string& message);

private:
    MainWindow();
    ~MainWindow();

    HINSTANCE h_instance_ = nullptr;
    HWND h_wnd_ = nullptr;

    // --- 顶部状态指示 ---
    HWND label_status_badge_ = nullptr;
    HWND label_active_model_ = nullptr;
    HWND label_reply_count_ = nullptr;

    // --- AI 配置控件 ---
    HWND edit_api_key_ = nullptr;
    HWND edit_base_url_ = nullptr;
    HWND combo_model_ = nullptr;
    HWND edit_system_prompt_ = nullptr;
    HWND edit_max_tokens_ = nullptr;
    HWND edit_history_rounds_ = nullptr;

    // --- OneBot 配置控件 ---
    HWND edit_ws_url_ = nullptr;
    HWND edit_bot_qq_ = nullptr;
    HWND edit_access_token_ = nullptr;
    HWND edit_prefixes_ = nullptr;
    HWND chk_group_require_at_ = nullptr;
    HWND edit_at_exempt_ = nullptr;

    // --- 插件管理区控件 ---
    HWND label_plugin_summary_ = nullptr;
    HWND btn_reload_plugins_ = nullptr;
    HWND btn_open_plugin_dir_ = nullptr;

    // --- 二级弹窗入口（点开才显示，主界面保持清爽）---
    HWND btn_advanced_ = nullptr;
    HWND btn_plugins_ = nullptr;
    HWND btn_help_ = nullptr;

    // --- 操作按钮 ---
    HWND btn_toggle_bot_ = nullptr;
    HWND btn_save_config_ = nullptr;
    HWND btn_test_api_ = nullptr;
    HWND btn_clear_log_ = nullptr;

    // --- 实时日志流 ---
    HWND edit_log_ = nullptr;

    // 运行状态与配置
    AppConfig config_;
    std::unique_ptr<BotClient> bot_client_;
    bool is_bot_running_ = false;
    int total_msg_handled_ = 0;
    std::mutex log_mutex_;
    std::vector<std::string> pending_logs_;

    // GDI 字体与画刷 (大字体舒适系统)
    HFONT font_title_ = nullptr;
    HFONT font_section_ = nullptr;
    HFONT font_body_ = nullptr;
    HFONT font_body_bold_ = nullptr;
    HFONT font_code_ = nullptr;
    HFONT font_log_ = nullptr;
    HFONT font_small_ = nullptr;

    HBRUSH brush_win_bg_ = nullptr;
    HBRUSH brush_card_bg_ = nullptr;
    HBRUSH brush_input_bg_ = nullptr;
    HBRUSH brush_log_bg_ = nullptr;

    void init_gdi_resources();
    void init_controls(HWND hwnd);
    void load_config_to_ui();
    void save_ui_to_config();
    void toggle_bot_state();
    void test_api_connection();
    void update_status_ui();
    void refresh_plugin_ui();
    void relayout_controls(int width, int height);

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK button_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
};
