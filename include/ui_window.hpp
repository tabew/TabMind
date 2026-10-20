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

// 主窗口：顶栏（品牌 + 主题/布局）+ 可自由拖拽/缩放的模块看板
class MainWindow {
public:
    static MainWindow& get_instance();

    bool create_and_show(HINSTANCE hInstance, int nCmdShow);
    void run_message_loop();
    void log(const std::string& message);

private:
    MainWindow();
    ~MainWindow();

    // ---------------- 模块定义 ----------------
    enum ModuleId {
        MOD_STATUS = 0,   // 运行状态
        MOD_AI,           // AI 接口配置
        MOD_QQ,           // OneBot / QQ 接入
        MOD_PLUGIN,       // 插件
        MOD_ACTION,       // 快捷操作
        MOD_LOG,          // 运行日志
        MOD_COUNT
    };

    HINSTANCE h_instance_ = nullptr;
    HWND h_wnd_ = nullptr;

    // ---------------- 顶栏 ----------------
    HWND btn_theme_ = nullptr;    // 切换主题
    HWND btn_layout_ = nullptr;   // 布局菜单

    // ---------------- 模块状态 ----------------
    RECT mod_rc_[MOD_COUNT] = {};
    bool mod_visible_[MOD_COUNT] = {};
    std::vector<HWND> mod_ctl_[MOD_COUNT];
    std::vector<int> mod_order_;   // 绘制/命中的 z 序（末尾=最上层）

    // 拖拽状态
    int drag_mode_ = 0;           // 0 无 / 1 移动 / 2 缩放
    int drag_mod_ = -1;
    POINT drag_start_ = {};
    RECT drag_rc0_ = {};

    // ---------------- AI 配置控件 ----------------
    HWND edit_api_key_ = nullptr;
    HWND edit_base_url_ = nullptr;
    HWND combo_model_ = nullptr;
    HWND edit_system_prompt_ = nullptr;
    HWND edit_max_tokens_ = nullptr;
    HWND edit_history_rounds_ = nullptr;

    // ---------------- OneBot 控件 ----------------
    HWND edit_ws_url_ = nullptr;
    HWND edit_bot_qq_ = nullptr;
    HWND edit_access_token_ = nullptr;
    HWND edit_prefixes_ = nullptr;
    HWND chk_group_require_at_ = nullptr;
    HWND edit_at_exempt_ = nullptr;

    // ---------------- 插件 / 操作控件 ----------------
    HWND label_plugin_summary_ = nullptr;
    HWND btn_reload_plugins_ = nullptr;
    HWND btn_open_plugin_dir_ = nullptr;
    HWND btn_plugin_mgr_ = nullptr;

    HWND btn_toggle_bot_ = nullptr;
    HWND btn_save_config_ = nullptr;
    HWND btn_test_api_ = nullptr;
    HWND btn_advanced_ = nullptr;
    HWND btn_help_ = nullptr;
    HWND btn_reset_layout_ = nullptr;

    // ---------------- 日志 ----------------
    HWND edit_log_ = nullptr;
    HWND btn_clear_log_ = nullptr;
    bool log_is_riched_ = false;

    // ---------------- 运行状态 ----------------
    AppConfig config_;
    std::unique_ptr<BotClient> bot_client_;
    bool is_bot_running_ = false;
    int total_msg_handled_ = 0;
    std::mutex log_mutex_;
    std::vector<std::string> pending_logs_;

    std::set<int> hovered_;   // owner-draw 悬停态

    // ---------------- GDI ----------------
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
    int applied_theme_ = -1;

    // ---------------- 内部方法 ----------------
    void init_gdi_resources();
    void apply_theme(int index, bool persist);
    void init_controls(HWND hwnd);

    void default_layout(int width, int height);
    void load_layout();
    void save_layout();
    void relayout_all();
    void relayout_module(int id);
    int module_at(POINT pt, bool& on_grip) const;
    void paint_module(HDC hdc, int id);
    static const wchar_t* module_title(int id);
    void show_layout_menu();

    void load_config_to_ui();
    void save_ui_to_config();
    void toggle_bot_state();
    void test_api_connection();
    void update_status_ui();
    void refresh_plugin_ui();
    void append_log_text(const std::wstring& text, const std::string& raw_for_scan);

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK button_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                 UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
};
