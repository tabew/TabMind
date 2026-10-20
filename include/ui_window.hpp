#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <memory>
#include <vector>
#include <set>
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

    // --- 顶部品牌区 ---
    HWND label_brand_ = nullptr;      // 标题（自绘卡片内绘制，这里仅保留占位）
    HWND label_status_badge_ = nullptr;
    HWND label_active_model_ = nullptr;
    HWND label_reply_count_ = nullptr;

    // --- 左侧标签页 ---
    HWND tab_btn_ai_ = nullptr;
    HWND tab_btn_ob_ = nullptr;
    HWND tab_btn_plg_ = nullptr;
    int active_tab_ = 0;              // 0=AI 1=接入 2=插件
    std::vector<HWND> tab0_ctl_;      // 每个标签页的控件集合，切页时 Show/Hide
    std::vector<HWND> tab1_ctl_;
    std::vector<HWND> tab2_ctl_;

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

    // --- 插件页控件 ---
    HWND label_plugin_summary_ = nullptr;
    HWND btn_reload_plugins_ = nullptr;
    HWND btn_open_plugin_dir_ = nullptr;

    // --- 弹窗入口 ---
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
    bool log_is_riched_ = false;

    // 运行状态与配置
    AppConfig config_;
    std::unique_ptr<BotClient> bot_client_;
    bool is_bot_running_ = false;
    int total_msg_handled_ = 0;
    std::mutex log_mutex_;
    std::vector<std::string> pending_logs_;

    // 悬停态（owner-draw 按钮用）
    std::set<int> hovered_;

    // GDI 资源
    HFONT font_brand_ = nullptr;
    HFONT font_h2_ = nullptr;
    HFONT font_body_ = nullptr;
    HFONT font_body_bold_ = nullptr;
    HFONT font_code_ = nullptr;
    HFONT font_log_ = nullptr;
    HFONT font_small_ = nullptr;

    HBRUSH brush_win_bg_ = nullptr;
    HBRUSH brush_card_bg_ = nullptr;
    HBRUSH brush_input_bg_ = nullptr;
    HBRUSH brush_field_bg_ = nullptr;
    HBRUSH brush_log_bg_ = nullptr;

    void init_gdi_resources();
    void init_controls(HWND hwnd);
    void switch_tab(int index);
    void load_config_to_ui();
    void save_ui_to_config();
    void toggle_bot_state();
    void test_api_connection();
    void update_status_ui();
    void refresh_plugin_ui();
    void relayout_controls(int width, int height);
    void append_log_text(const std::wstring& text, const std::string& raw_for_scan);

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK button_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
};
