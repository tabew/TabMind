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
#define WM_USER_MODULE_CLOSED (WM_USER + 103)   // 模块窗口被关闭（通知菜单刷新）

// TabMind 主窗口 = 一个小型「菜单」，功能块各自独立成窗口。
// 每个窗口只绘制自己那一块，拖动/缩放交给系统，不再整窗重绘，因此不会卡。
class MainWindow {
public:
    static MainWindow& get_instance();

    bool create_and_show(HINSTANCE hInstance, int nCmdShow);
    void run_message_loop();
    void log(const std::string& message);

    // ---- 供模块窗口使用 ----
    AppConfig& config() { return config_; }
    HFONT font_body() const { return font_body_; }
    HFONT font_body_bold() const { return font_body_bold_; }
    HFONT font_small() const { return font_small_; }
    HFONT font_h2() const { return font_h2_; }
    HBRUSH brush_card() const { return brush_card_bg_; }
    HBRUSH brush_field() const { return brush_field_bg_; }
    HBRUSH brush_log() const { return brush_log_bg_; }
    bool is_running() const { return is_bot_running_; }
    int msg_count() const { return total_msg_handled_; }
    bool log_is_riched() const { return log_is_riched_; }
    HWND log_edit() const { return edit_log_; }

    void action_toggle_bot();
    void action_save_config();
    void action_test_api();
    void refresh_plugin_ui();

private:
    MainWindow();
    ~MainWindow();

    enum ModuleId {
        MOD_STATUS = 0,   // 运行状态
        MOD_AI,           // AI 接口配置
        MOD_QQ,           // OneBot / QQ 接入
        MOD_PLUGIN,       // 扩展插件
        MOD_ACTION,       // 快捷操作
        MOD_LOG,          // 运行日志
        MOD_COUNT
    };

    struct ModuleCtx {
        MainWindow* owner = nullptr;
        int kind = 0;
    };

    HINSTANCE h_instance_ = nullptr;
    HWND h_wnd_ = nullptr;                       // 菜单窗口

    HWND btn_theme_ = nullptr;
    int hover_row_ = -1;

    HWND mod_wnd_[MOD_COUNT] = {};
    bool mod_open_[MOD_COUNT] = {};
    RECT mod_rect_[MOD_COUNT] = {};
    std::vector<HWND> mod_ctl_[MOD_COUNT];

    // AI
    HWND edit_api_key_ = nullptr;
    HWND edit_base_url_ = nullptr;
    HWND combo_model_ = nullptr;
    HWND edit_system_prompt_ = nullptr;
    HWND edit_max_tokens_ = nullptr;
    HWND edit_history_rounds_ = nullptr;
    // OneBot
    HWND edit_ws_url_ = nullptr;
    HWND edit_bot_qq_ = nullptr;
    HWND edit_access_token_ = nullptr;
    HWND edit_prefixes_ = nullptr;
    HWND chk_group_require_at_ = nullptr;
    HWND edit_at_exempt_ = nullptr;
    // 插件
    HWND label_plugin_summary_ = nullptr;
    HWND btn_reload_plugins_ = nullptr;
    HWND btn_open_plugin_dir_ = nullptr;
    HWND btn_plugin_mgr_ = nullptr;
    // 操作
    HWND btn_toggle_bot_ = nullptr;
    HWND btn_save_config_ = nullptr;
    HWND btn_test_api_ = nullptr;
    HWND btn_advanced_ = nullptr;
    HWND btn_help_ = nullptr;
    // 日志
    HWND edit_log_ = nullptr;
    HWND btn_clear_log_ = nullptr;
    bool log_is_riched_ = false;

    // 运行状态
    AppConfig config_;
    std::unique_ptr<BotClient> bot_client_;
    bool is_bot_running_ = false;
    int total_msg_handled_ = 0;
    std::mutex log_mutex_;
    std::vector<std::string> pending_logs_;

    std::set<int> hovered_;

    // GDI
    HFONT font_brand_ = nullptr;
    HFONT font_h2_ = nullptr;
    HFONT font_body_ = nullptr;
    HFONT font_body_bold_ = nullptr;
    HFONT font_small_ = nullptr;
    HFONT font_log_ = nullptr;
    HBRUSH brush_win_bg_ = nullptr;
    HBRUSH brush_card_bg_ = nullptr;
    HBRUSH brush_field_bg_ = nullptr;
    HBRUSH brush_log_bg_ = nullptr;

    void init_gdi_resources();
    void apply_theme(int index, bool persist);

    void init_launcher(HWND hwnd);
    void paint_launcher(HDC hdc, const RECT& rc);
    int launcher_row_at(POINT pt) const;
    void toggle_module(int kind);

    void create_module_windows();
    void create_module(int kind);
    void show_module(int kind, bool open);
    void relayout_module(int kind);
    void paint_module_window(int kind, HDC hdc, const RECT& rc);
    RECT default_module_rect(int kind) const;

    void load_config_to_ui();
    void save_ui_to_config();
    void update_status_ui();
    void append_log_text(const std::wstring& text, const std::string& raw_for_scan);

    static const wchar_t* module_title(int kind);

    static LRESULT CALLBACK launcher_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK module_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK button_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                 UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
};
