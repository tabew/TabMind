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

// 单窗口 + 左侧菜单 + 右侧嵌入式卡片。
// 每个功能块是一个 WS_CHILD 子窗口：拖动只移动该子窗口，父窗口不重绘，
// 所以既有「自由摆放」的手感，又不会像整窗重绘那样卡。
class MainWindow {
public:
    static MainWindow& get_instance();

    bool create_and_show(HINSTANCE hInstance, int nCmdShow);
    void run_message_loop();
    void log(const std::string& message);

    // ---- 供模块子窗口使用 ----
    AppConfig& config() { return config_; }
    HFONT font_body() const { return font_body_; }
    HFONT font_body_bold() const { return font_body_bold_; }
    HFONT font_small() const { return font_small_; }
    HFONT font_h2() const { return font_h2_; }
    HBRUSH brush_win() const { return brush_win_bg_; }
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
    HWND h_wnd_ = nullptr;

    HWND btn_theme_ = nullptr;
    int hover_row_ = -1;

    HWND mod_wnd_[MOD_COUNT] = {};
    bool mod_open_[MOD_COUNT] = {};
    RECT mod_rect_[MOD_COUNT] = {};      // 主窗口客户区坐标
    std::vector<HWND> mod_ctl_[MOD_COUNT];

    // 卡片拖动/缩放状态
    int drag_mode_ = 0;                  // 0 无 / 1 移动 / 2 缩放
    int drag_mod_ = -1;
    POINT drag_pt_ = {};
    RECT drag_rc0_ = {};

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
    HWND btn_reset_layout_ = nullptr;
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

    void init_sidebar(HWND hwnd);
    void paint_main(HDC hdc, const RECT& rc);
    int sidebar_row_at(POINT pt) const;

    void create_module_windows();
    void create_module(int kind);
    void show_module(int kind, bool open);
    void bring_to_front(int kind);
    void relayout_module(int kind);
    void paint_module_card(int kind, HDC hdc, const RECT& rc);
    RECT default_module_rect(int kind) const;
    void clamp_module(int kind);
    void save_layout();

    void load_config_to_ui();
    void save_ui_to_config();
    void update_status_ui();
    void append_log_text(const std::wstring& text, const std::string& raw_for_scan);

    static const wchar_t* module_title(int kind);

    static LRESULT CALLBACK main_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK module_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK button_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                 UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
};
