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
#include "ui_theme.hpp"

#define WM_USER_LOG (WM_USER + 101)

// ---------------------------------------------------------------------------
// TabMind UI v3
//
// 单主窗口 + 左侧菜单 + 右侧自由摆放的嵌入式卡片。
//
// 性能要点（每一条都对应一个真实卡顿源）：
//   A. 绘制用「内存 DC → BitBlt」双缓冲，且位图按无效区大小分配并跨帧复用，
//      不做每帧 CreateCompatibleBitmap / DeleteObject；
//   B. 画刷、画笔按主题缓存在 ui_gdi()，绘制路径里零 GDI 对象创建销毁；
//   C. 日志是自绘 LISTBOX，只画可见行；追加是 O(1) 尾部插入；
//   D. 状态刷新走 400ms 定时器 + 脏标记，不再每条日志全窗口 Invalidate；
//   E. 拖动卡片时隐藏卡内控件，只搬 1 个窗口；松手用 DeferWindowPos 原子复位；
//   F. 卡片 z 序惰性置顶，已置顶则完全不动作；
//   G. 侧栏 hover 只重画受影响的两行。
// ---------------------------------------------------------------------------
class MainWindow {
public:
    static MainWindow& get_instance();

    bool create_and_show(HINSTANCE hInstance, int nCmdShow);
    void run_message_loop();
    void log(const std::string& message);

    AppConfig& config() { return config_; }
    HFONT font_body() const { return font_body_; }
    HFONT font_body_bold() const { return font_body_bold_; }
    HFONT font_small() const { return font_small_; }
    HFONT font_h2() const { return font_h2_; }
    HBRUSH brush_win() const { return ui_gdi().br_win; }
    HBRUSH brush_card() const { return ui_gdi().br_card; }
    HBRUSH brush_field() const { return ui_gdi().br_field; }
    HBRUSH brush_log() const { return ui_gdi().br_log; }
    bool is_running() const { return is_bot_running_; }
    int msg_count() const { return total_msg_handled_; }

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
    RECT mod_rect_[MOD_COUNT] = {};                  // 主窗口客户区坐标
    std::vector<HWND> mod_ctl_[MOD_COUNT];
    int z_order_[MOD_COUNT] = { 0, 1, 2, 3, 4, 5 };  // 由下到上

    // 拖动/缩放状态
    int drag_mode_ = 0;                // 0 无 / 1 移动 / 2 缩放
    int drag_mod_ = -1;
    POINT drag_pt_ = {};               // 按下时的卡片客户区坐标（仅用于命中测试）
    POINT drag_screen_pt_ = {};        // 按下时的屏幕坐标 —— 拖动位移必须用它算
    RECT drag_rc0_ = {};               // 按下时的卡片矩形（主窗口客户区坐标）

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
    std::vector<PluginInfo> plugin_cache_;
    // 操作
    HWND btn_toggle_bot_ = nullptr;
    HWND btn_save_config_ = nullptr;
    HWND btn_test_api_ = nullptr;
    HWND btn_advanced_ = nullptr;
    HWND btn_help_ = nullptr;
    HWND btn_reset_layout_ = nullptr;
    // 日志（自绘 LISTBOX）
    HWND list_log_ = nullptr;
    HWND btn_clear_log_ = nullptr;
    bool log_follow_ = true;

    AppConfig config_;
    std::unique_ptr<BotClient> bot_client_;
    bool is_bot_running_ = false;
    int total_msg_handled_ = 0;
    bool status_dirty_ = false;
    bool destroying_ = false;   // WM_DESTROY 期间：不再对卡片窗口做重绘操作
    std::mutex log_mutex_;
    std::vector<std::string> pending_logs_;

    std::vector<int> hovered_;

    HFONT font_brand_ = nullptr;
    HFONT font_h2_ = nullptr;
    HFONT font_body_ = nullptr;
    HFONT font_body_bold_ = nullptr;
    HFONT font_small_ = nullptr;
    HFONT font_log_ = nullptr;

    void init_gdi_resources();
    void apply_theme(int index, bool persist);

    void init_sidebar(HWND hwnd);
    void paint_main(HDC hdc, const RECT& rc, const RECT& rcPaint);
    int sidebar_row_at(POINT pt) const;
    RECT sidebar_row_rect(int row) const;

    void create_module_windows();
    void create_module(int kind);
    void show_module(int kind, bool open);
    void bring_to_front(int kind);
    void apply_z_order();
    void set_card_controls_visible(int kind, bool show);
    void relayout_module(int kind);
    void paint_module_card(int kind, HDC hdc, const RECT& rc, const RECT& rcPaint);
    RECT default_module_rect(int kind) const;
    void clamp_module(int kind);
    void save_layout();

    void load_config_to_ui();
    void save_ui_to_config();
    void update_status_ui();
    void abort_drag();
    void log_scroll_bottom();
    void log_update_follow();

    static const wchar_t* module_title(int kind);

    static LRESULT CALLBACK main_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK module_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK button_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                 UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
};
