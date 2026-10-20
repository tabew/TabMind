#include "ui_window.hpp"
#include "plugin_manager.hpp"
#include <dwmapi.h>
#include <uxtheme.h>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <shellapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "msimg32.lib")

// 控件 ID
#define IDC_BTN_TOGGLE_BOT       1001
#define IDC_BTN_SAVE_CONFIG      1002
#define IDC_BTN_TEST_API         1003
#define IDC_BTN_CLEAR_LOG        1004
#define IDC_BTN_RELOAD_PLUGINS   1005
#define IDC_BTN_OPEN_PLUGIN_DIR  1006

#define IDC_EDIT_API_KEY         1101
#define IDC_EDIT_BASE_URL        1102
#define IDC_COMBO_MODEL          1103
#define IDC_EDIT_SYSTEM_PROMPT   1104
#define IDC_EDIT_MAX_TOKENS      1105
#define IDC_EDIT_HISTORY_ROUNDS  1106

#define IDC_EDIT_WS_URL          1201
#define IDC_EDIT_BOT_QQ          1202
#define IDC_EDIT_ACCESS_TOKEN    1203
#define IDC_EDIT_PREFIXES        1204
#define IDC_CHK_REQUIRE_AT       1205
#define IDC_EDIT_AT_EXEMPT       1206

#define IDC_EDIT_LOG             1301

// 配色设计 (Modern Clean & High Contrast)
namespace Theme {
    const COLORREF WinBg         = RGB(245, 247, 250); // #F5F7FA 浅灰舒适背景
    const COLORREF HeaderBg      = RGB(255, 255, 255); // #FFFFFF 顶栏白底
    const COLORREF HeaderBorder  = RGB(226, 232, 240); // #E2E8F0
    const COLORREF CardBg        = RGB(255, 255, 255); // #FFFFFF 卡片背景
    const COLORREF CardBorder    = RGB(229, 231, 235); // #E5E7EB 卡片边框
    const COLORREF InputBg       = RGB(255, 255, 255); // #FFFFFF
    const COLORREF PrimaryBlue   = RGB(37, 99, 235);   // #2563EB 品牌蓝
    const COLORREF PrimaryHover  = RGB(29, 78, 216);   // #1D4ED8
    const COLORREF SuccessGreen  = RGB(16, 185, 129);  // #10B981 运行绿
    const COLORREF DangerRed     = RGB(239, 68, 68);   // #EF4444 停止红
    const COLORREF DangerHover   = RGB(220, 38, 38);
    const COLORREF TextMain      = RGB(30, 41, 59);    // #1E293B 正文字色
    const COLORREF TextMuted     = RGB(100, 116, 139); // #64748B 辅助说明
    const COLORREF LogBg         = RGB(15, 23, 42);    // #0F172A 控制台黑底
    const COLORREF LogText       = RGB(241, 245, 249); // #F1F5F9 日志高亮白
}

static std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size);
    return wstr;
}

static std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size, NULL, NULL);
    return str;
}

static void set_window_text_u8(HWND hwnd, const std::string& text) {
    if (hwnd) SetWindowTextW(hwnd, utf8_to_wstring(text).c_str());
}

static std::string get_window_text_u8(HWND hwnd) {
    if (!hwnd) return "";
    int len = GetWindowTextLengthW(hwnd);
    if (len == 0) return "";
    std::vector<wchar_t> buf(len + 1);
    GetWindowTextW(hwnd, buf.data(), len + 1);
    return wstring_to_utf8(std::wstring(buf.data()));
}

static void draw_rounded_rect(HDC hdc, const RECT& rc, int radius, COLORREF fillColor, COLORREF borderColor = CLR_INVALID) {
    HBRUSH hBrush = CreateSolidBrush(fillColor);
    HPEN hPen = (borderColor == CLR_INVALID) ? CreatePen(PS_NULL, 0, 0) : CreatePen(PS_SOLID, 1, borderColor);
    HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}

MainWindow::MainWindow() {}

MainWindow::~MainWindow() {
    if (font_title_) DeleteObject(font_title_);
    if (font_section_) DeleteObject(font_section_);
    if (font_body_) DeleteObject(font_body_);
    if (font_body_bold_) DeleteObject(font_body_bold_);
    if (font_code_) DeleteObject(font_code_);
    if (font_small_) DeleteObject(font_small_);

    if (brush_win_bg_) DeleteObject(brush_win_bg_);
    if (brush_card_bg_) DeleteObject(brush_card_bg_);
    if (brush_input_bg_) DeleteObject(brush_input_bg_);
    if (brush_log_bg_) DeleteObject(brush_log_bg_);
}

MainWindow& MainWindow::get_instance() {
    static MainWindow instance;
    return instance;
}

void MainWindow::init_gdi_resources() {
    // 字体字号设计
    font_title_ = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");

    font_section_ = CreateFontW(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");

    font_body_ = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");

    font_body_bold_ = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");

    font_small_ = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");

    font_code_ = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Consolas");

    brush_win_bg_ = CreateSolidBrush(Theme::WinBg);
    brush_card_bg_ = CreateSolidBrush(Theme::CardBg);
    brush_input_bg_ = CreateSolidBrush(Theme::InputBg);
    brush_log_bg_ = CreateSolidBrush(Theme::LogBg);
}

LRESULT CALLBACK MainWindow::button_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uMsg == WM_ERASEBKGND) return 1;
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

bool MainWindow::create_and_show(HINSTANCE hInstance, int nCmdShow) {
    h_instance_ = hInstance;

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    init_gdi_resources();

    const wchar_t CLASS_NAME[] = L"QQBotAI_Resizable_Window";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWindow::window_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = brush_win_bg_;
    RegisterClassW(&wc);

    // 默认高宽 1200 x 860，并支持 WS_THICKFRAME (拖动缩放) 和 WS_MAXIMIZEBOX (全屏最大化)
    int win_w = 1200;
    int win_h = 860;
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int pos_x = (screen_w - win_w) / 2;
    int pos_y = (screen_h - win_h) / 2;

    h_wnd_ = CreateWindowExW(
        0,
        CLASS_NAME,
        L"QQ Bot AI - 智能大模型聊天机器人管理终端 (支持C++插件扩展)",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        pos_x, pos_y, win_w, win_h,
        NULL, NULL, hInstance, this
    );

    if (!h_wnd_) return false;

    config_ = AppConfig::load_from_file("config.json");
    load_config_to_ui();
    update_status_ui();

    // 自动装载 ./plugins 插件
    PluginManager::get_instance().load_plugins("plugins");
    refresh_plugin_ui();

    ShowWindow(h_wnd_, nCmdShow);
    UpdateWindow(h_wnd_);

    log("🎉 QQ Bot AI 管理终端已就绪。");
    log("📌 OneBot 服务端监听地址: " + config_.onebot.ws_url);

    // 自动连接并启动机器人（无需手动点击按钮，方便服务器无人值守自启）
    if (!config_.ai.api_key.empty()) {
        log("🚀 正在自动连接 OneBot WebSocket 服务...");
        bot_client_ = std::make_unique<BotClient>(config_);
        bot_client_->start();
        is_bot_running_ = true;
        update_status_ui();
        log("√ 机器人已自动上线！正在监听消息...");
    } else {
        log("⚠️ 检测到未配置 AI API Key，请在左侧填写后点击启动按钮。");
    }

    return true;
}

void MainWindow::init_controls(HWND hwnd) {
    auto create_lbl = [&](const wchar_t* text, int x, int y, int w, int h, HFONT font = nullptr) {
        HWND hLbl = CreateWindowW(L"STATIC", text, WS_VISIBLE | WS_CHILD, x, y, w, h, hwnd, NULL, h_instance_, NULL);
        SendMessageW(hLbl, WM_SETFONT, (WPARAM)(font ? font : font_body_), TRUE);
        return hLbl;
    };

    auto create_edit = [&](int id, int x, int y, int w, int h, DWORD style = 0) {
        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | style,
            x, y, w, h, hwnd, (HMENU)(INT_PTR)id, h_instance_, NULL);
        SendMessageW(hEdit, WM_SETFONT, (WPARAM)font_body_, TRUE);
        return hEdit;
    };

    // =========================================================================
    // 【顶部状态指示条】 Y: 14
    // =========================================================================
    label_status_badge_ = CreateWindowW(L"STATIC", L"● 状态: 未连接", WS_VISIBLE | WS_CHILD,
        25, 14, 220, 32, hwnd, NULL, h_instance_, NULL);
    SendMessageW(label_status_badge_, WM_SETFONT, (WPARAM)font_body_bold_, TRUE);

    label_active_model_ = CreateWindowW(L"STATIC", L"🤖 当前模型: deepseek-chat", WS_VISIBLE | WS_CHILD,
        260, 14, 380, 32, hwnd, NULL, h_instance_, NULL);
    SendMessageW(label_active_model_, WM_SETFONT, (WPARAM)font_body_, TRUE);

    label_reply_count_ = CreateWindowW(L"STATIC", L"💬 已回复消息: 0 条", WS_VISIBLE | WS_CHILD,
        660, 14, 240, 32, hwnd, NULL, h_instance_, NULL);
    SendMessageW(label_reply_count_, WM_SETFONT, (WPARAM)font_body_, TRUE);

    // =========================================================================
    // 【左半区：核心配置与插件系统】 绝无重叠的清晰单列排版
    // =========================================================================
    int left_x = 35;
    int left_w = 515;

    // --- 1. AI 接口配置卡片 (Y: 65 ~ 380) ---
    int cy = 100;
    create_lbl(L"API 接口地址 (Base URL):", left_x, cy, 240, 18, font_small_);
    edit_base_url_ = create_edit(IDC_EDIT_BASE_URL, left_x, cy + 20, left_w, 28);

    cy += 54;
    create_lbl(L"API 密钥 (API Key):", left_x, cy, 240, 18, font_small_);
    edit_api_key_ = create_edit(IDC_EDIT_API_KEY, left_x, cy + 20, left_w, 28, ES_PASSWORD);

    cy += 54;
    create_lbl(L"模型名称 (Model):", left_x, cy, 180, 18, font_small_);
    combo_model_ = CreateWindowW(L"COMBOBOX", L"",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWN | WS_VSCROLL,
        left_x, cy + 20, 220, 200, hwnd, (HMENU)IDC_COMBO_MODEL, h_instance_, NULL);
    SendMessageW(combo_model_, WM_SETFONT, (WPARAM)font_body_, TRUE);

    SendMessageW(combo_model_, CB_ADDSTRING, 0, (LPARAM)L"deepseek-chat");
    SendMessageW(combo_model_, CB_ADDSTRING, 0, (LPARAM)L"deepseek-reasoner");
    SendMessageW(combo_model_, CB_ADDSTRING, 0, (LPARAM)L"deepseek-v4-flash");
    SendMessageW(combo_model_, CB_ADDSTRING, 0, (LPARAM)L"gpt-4o-mini");
    SendMessageW(combo_model_, CB_ADDSTRING, 0, (LPARAM)L"gpt-4o");

    create_lbl(L"Max Tokens:", left_x + 235, cy, 120, 18, font_small_);
    edit_max_tokens_ = create_edit(IDC_EDIT_MAX_TOKENS, left_x + 235, cy + 20, 130, 28, ES_NUMBER);

    create_lbl(L"上下文轮数:", left_x + 380, cy, 120, 18, font_small_);
    edit_history_rounds_ = create_edit(IDC_EDIT_HISTORY_ROUNDS, left_x + 380, cy + 20, 135, 28, ES_NUMBER);

    cy += 54;
    create_lbl(L"System Prompt (系统提示词 / 自动换行人设):", left_x, cy, 380, 18, font_small_);
    // 关键修复：移除 ES_AUTOHSCROLL，增加 ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL 实现自动换行
    edit_system_prompt_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        left_x, cy + 20, left_w, 75, hwnd, (HMENU)IDC_EDIT_SYSTEM_PROMPT, h_instance_, NULL);
    SendMessageW(edit_system_prompt_, WM_SETFONT, (WPARAM)font_body_, TRUE);

    // --- 2. OneBot 接入配置卡片 (Y: 395 ~ 635) ---
    cy = 430;
    create_lbl(L"OneBot WS 服务端地址:", left_x, cy, 240, 18, font_small_);
    edit_ws_url_ = create_edit(IDC_EDIT_WS_URL, left_x, cy + 20, left_w, 28);

    cy += 54;
    create_lbl(L"机器人 QQ 号 (Bot QQ):", left_x, cy, 220, 18, font_small_);
    edit_bot_qq_ = create_edit(IDC_EDIT_BOT_QQ, left_x, cy + 20, 245, 28, ES_NUMBER);

    create_lbl(L"Access Token (无则留空):", left_x + 260, cy, 220, 18, font_small_);
    edit_access_token_ = create_edit(IDC_EDIT_ACCESS_TOKEN, left_x + 260, cy + 20, 255, 28);

    cy += 54;
    create_lbl(L"触发前缀词 (逗号分隔，如: /chat, /ai, 你好):", left_x, cy, 400, 18, font_small_);
    edit_prefixes_ = create_edit(IDC_EDIT_PREFIXES, left_x, cy + 20, left_w, 28);

    cy += 54;
    chk_group_require_at_ = CreateWindowW(L"BUTTON", L" 群聊必须 @机器人 或指令前缀才响应 (免@插件除外，防刷屏)",
        WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
        left_x, cy, left_w, 24, hwnd, (HMENU)IDC_CHK_REQUIRE_AT, h_instance_, NULL);
    SendMessageW(chk_group_require_at_, WM_SETFONT, (WPARAM)font_body_, TRUE);

    cy += 30;
    create_lbl(L"免@插件 (DLL名/插件名关键词，逗号分隔):", left_x, cy + 3, 300, 16, font_small_);
    edit_at_exempt_ = create_edit(IDC_EDIT_AT_EXEMPT, left_x + 305, cy, 210, 24);

    // --- 3. 插件管理卡片 (Y: 650 ~ 725) ---
    cy = 665;
    label_plugin_summary_ = CreateWindowW(L"STATIC", L"🔌 扩展插件: 已加载 0 个插件 (DLL 放置在 ./plugins 目录)",
        WS_VISIBLE | WS_CHILD,
        left_x, cy, 310, 36, hwnd, NULL, h_instance_, NULL);
    SendMessageW(label_plugin_summary_, WM_SETFONT, (WPARAM)font_small_, TRUE);

    btn_reload_plugins_ = CreateWindowW(L"BUTTON", L"🔄 重载插件",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        left_x + 315, cy, 95, 34, hwnd, (HMENU)IDC_BTN_RELOAD_PLUGINS, h_instance_, NULL);
    SetWindowSubclass(btn_reload_plugins_, MainWindow::button_subclass_proc, IDC_BTN_RELOAD_PLUGINS, 0);

    btn_open_plugin_dir_ = CreateWindowW(L"BUTTON", L"📂 插件目录",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        left_x + 418, cy, 95, 34, hwnd, (HMENU)IDC_BTN_OPEN_PLUGIN_DIR, h_instance_, NULL);
    SetWindowSubclass(btn_open_plugin_dir_, MainWindow::button_subclass_proc, IDC_BTN_OPEN_PLUGIN_DIR, 0);

    // --- 4. 配置保存与快速测试按钮 (Y: 745 ~ 795) ---
    btn_save_config_ = CreateWindowW(L"BUTTON", L"💾 保存所有配置",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        25, 745, 275, 46, hwnd, (HMENU)IDC_BTN_SAVE_CONFIG, h_instance_, NULL);
    SetWindowSubclass(btn_save_config_, MainWindow::button_subclass_proc, IDC_BTN_SAVE_CONFIG, 0);

    btn_test_api_ = CreateWindowW(L"BUTTON", L"⚡ 测试 API 连通性",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        310, 745, 270, 46, hwnd, (HMENU)IDC_BTN_TEST_API, h_instance_, NULL);
    SetWindowSubclass(btn_test_api_, MainWindow::button_subclass_proc, IDC_BTN_TEST_API, 0);

    // =========================================================================
    // 【右半区：操作主控与大屏日志流】
    // =========================================================================
    btn_toggle_bot_ = CreateWindowW(L"BUTTON", L"🚀 启动 AI 智能机器人",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        605, 65, 420, 50, hwnd, (HMENU)IDC_BTN_TOGGLE_BOT, h_instance_, NULL);
    SetWindowSubclass(btn_toggle_bot_, MainWindow::button_subclass_proc, IDC_BTN_TOGGLE_BOT, 0);

    btn_clear_log_ = CreateWindowW(L"BUTTON", L"🧹 清空日志",
        WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        1035, 65, 130, 50, hwnd, (HMENU)IDC_BTN_CLEAR_LOG, h_instance_, NULL);
    SetWindowSubclass(btn_clear_log_, MainWindow::button_subclass_proc, IDC_BTN_CLEAR_LOG, 0);

    edit_log_ = CreateWindowExW(0, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        605, 155, 560, 636, hwnd, (HMENU)IDC_EDIT_LOG, h_instance_, NULL);
    SendMessageW(edit_log_, EM_LIMITTEXT, 0, 0); // 取消 Win32 Edit 控件 32KB/64KB 字符限制
    SendMessageW(edit_log_, WM_SETFONT, (WPARAM)font_code_, TRUE);
}

void MainWindow::relayout_controls(int width, int height) {
    if (width < 800 || height < 600) return;

    // 计算右侧自适应日志区域宽度
    int right_x = 605;
    int right_w = width - right_x - 25;
    if (right_w < 300) right_w = 300;

    int log_h = height - 175;
    if (log_h < 300) log_h = 300;

    int btn_toggle_w = right_w - 145;
    if (btn_toggle_w < 180) btn_toggle_w = 180;

    SetWindowPos(btn_toggle_bot_, NULL, right_x, 65, btn_toggle_w, 50, SWP_NOZORDER);
    SetWindowPos(btn_clear_log_, NULL, right_x + btn_toggle_w + 10, 65, 130, 50, SWP_NOZORDER);
    SetWindowPos(edit_log_, NULL, right_x, 155, right_w, log_h, SWP_NOZORDER);

    InvalidateRect(h_wnd_, NULL, TRUE);
}

void MainWindow::refresh_plugin_ui() {
    auto list = PluginManager::get_instance().get_plugin_list();
    std::wstring txt = L"🔌 扩展插件: 已加载 " + std::to_wstring(list.size()) + L" 个插件 (放置 DLL 在 ./plugins)";
    if (label_plugin_summary_) {
        SetWindowTextW(label_plugin_summary_, txt.c_str());
    }
}

void MainWindow::update_status_ui() {
    if (label_status_badge_) {
        std::wstring text = is_bot_running_ ? L"● 状态: 运行中 (监听消息中)" : L"● 状态: 已停止";
        SetWindowTextW(label_status_badge_, text.c_str());
    }

    if (label_active_model_) {
        std::wstring m = L"🤖 当前模型: " + utf8_to_wstring(config_.ai.model);
        SetWindowTextW(label_active_model_, m.c_str());
    }

    if (label_reply_count_) {
        std::wstring c = L"💬 已回复消息: " + std::to_wstring(total_msg_handled_) + L" 条";
        SetWindowTextW(label_reply_count_, c.c_str());
    }

    if (btn_toggle_bot_) {
        InvalidateRect(btn_toggle_bot_, NULL, TRUE);
    }
}

void MainWindow::load_config_to_ui() {
    set_window_text_u8(edit_api_key_, config_.ai.api_key);
    set_window_text_u8(edit_base_url_, config_.ai.base_url);
    set_window_text_u8(combo_model_, config_.ai.model);
    set_window_text_u8(edit_system_prompt_, config_.ai.system_prompt);
    set_window_text_u8(edit_max_tokens_, std::to_string(config_.ai.max_tokens));
    set_window_text_u8(edit_history_rounds_, std::to_string(config_.ai.max_history_rounds));

    set_window_text_u8(edit_ws_url_, config_.onebot.ws_url);
    set_window_text_u8(edit_bot_qq_, std::to_string(config_.onebot.bot_qq));
    set_window_text_u8(edit_access_token_, config_.onebot.access_token);

    SendMessageW(chk_group_require_at_, BM_SETCHECK, config_.onebot.group_require_at ? BST_CHECKED : BST_UNCHECKED, 0);

    {
        std::string exempt;
        for (size_t i = 0; i < config_.onebot.at_exempt_plugins.size(); ++i) {
            exempt += config_.onebot.at_exempt_plugins[i];
            if (i + 1 < config_.onebot.at_exempt_plugins.size()) exempt += ", ";
        }
        set_window_text_u8(edit_at_exempt_, exempt);
    }

    std::string prefixes;
    for (size_t i = 0; i < config_.onebot.trigger_prefixes.size(); ++i) {
        prefixes += config_.onebot.trigger_prefixes[i];
        if (i + 1 < config_.onebot.trigger_prefixes.size()) prefixes += ", ";
    }
    set_window_text_u8(edit_prefixes_, prefixes);
}

void MainWindow::save_ui_to_config() {
    config_.ai.api_key = get_window_text_u8(edit_api_key_);
    config_.ai.base_url = get_window_text_u8(edit_base_url_);
    config_.ai.model = get_window_text_u8(combo_model_);
    config_.ai.system_prompt = get_window_text_u8(edit_system_prompt_);

    try {
        config_.ai.max_tokens = std::stoi(get_window_text_u8(edit_max_tokens_));
    } catch (...) { config_.ai.max_tokens = 1000; }

    try {
        config_.ai.max_history_rounds = std::stoi(get_window_text_u8(edit_history_rounds_));
    } catch (...) { config_.ai.max_history_rounds = 10; }

    config_.onebot.ws_url = get_window_text_u8(edit_ws_url_);
    config_.onebot.access_token = get_window_text_u8(edit_access_token_);

    try {
        config_.onebot.bot_qq = std::stoll(get_window_text_u8(edit_bot_qq_));
    } catch (...) { config_.onebot.bot_qq = 0; }

    config_.onebot.group_require_at = (SendMessageW(chk_group_require_at_, BM_GETCHECK, 0, 0) == BST_CHECKED);

    config_.onebot.at_exempt_plugins.clear();
    {
        std::stringstream es(get_window_text_u8(edit_at_exempt_));
        std::string item;
        while (std::getline(es, item, ',')) {
            auto start = item.find_first_not_of(" \t\r\n");
            auto end = item.find_last_not_of(" \t\r\n");
            if (start != std::string::npos) {
                config_.onebot.at_exempt_plugins.push_back(item.substr(start, end - start + 1));
            }
        }
    }

    std::string prefixes_str = get_window_text_u8(edit_prefixes_);
    config_.onebot.trigger_prefixes.clear();
    std::stringstream ss(prefixes_str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        auto start = item.find_first_not_of(" \t\r\n");
        auto end = item.find_last_not_of(" \t\r\n");
        if (start != std::string::npos) {
            config_.onebot.trigger_prefixes.push_back(item.substr(start, end - start + 1));
        }
    }

    config_.save_to_file("config.json");
    log("√ 配置已成功同步保存至 config.json");
    update_status_ui();
}

void MainWindow::toggle_bot_state() {
    if (is_bot_running_) {
        if (bot_client_) {
            bot_client_->stop();
            bot_client_.reset();
        }
        is_bot_running_ = false;
        update_status_ui();
        log("⭕ 机器人已安全停止。");
    } else {
        save_ui_to_config();

        if (config_.ai.api_key.empty()) {
            MessageBoxW(h_wnd_, L"请先在左侧输入框填写 AI API Key！", L"提示", MB_OK | MB_ICONWARNING);
            return;
        }

        log("🚀 正在建立 OneBot WebSocket 连接: " + config_.onebot.ws_url + " ...");
        bot_client_ = std::make_unique<BotClient>(config_);
        bot_client_->start();
        is_bot_running_ = true;
        update_status_ui();
        log("√ 机器人已上线！正在监听 OneBot 消息事件...");
    }
}

void MainWindow::test_api_connection() {
    save_ui_to_config();
    log("📡 正在向 AI 服务端 (" + config_.ai.base_url + ") 发送连通性测试...");

    std::thread([this]() {
        AIClient client(config_.ai);
        std::string reply = client.chat("test_ping", "请回复两个字：收到");
        if (reply.find("【AI") != std::string::npos) {
            log("❌ API 测试失败: " + reply);
            MessageBoxW(h_wnd_, utf8_to_wstring("API 测试连接失败，错误详情:\r\n" + reply).c_str(), L"API 测试失败", MB_OK | MB_ICONERROR);
        } else {
            log("✅ API 接口测试通过！模型响应: " + reply);
            MessageBoxW(h_wnd_, L"🎉 API 连通性测试成功！大模型已就绪。", L"测试成功", MB_OK | MB_ICONINFORMATION);
        }
    }).detach();
}

void MainWindow::log(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        pending_logs_.push_back(message);
    }
    if (h_wnd_) {
        PostMessageW(h_wnd_, WM_USER_LOG, 0, 0);
    }
}

void MainWindow::run_message_loop() {
    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

LRESULT CALLBACK MainWindow::window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = (MainWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (uMsg) {
    case WM_USER_LOG: {
        if (!self || !self->edit_log_) return 0;
        std::vector<std::string> logs_to_flush;
        {
            std::lock_guard<std::mutex> lock(self->log_mutex_);
            logs_to_flush.swap(self->pending_logs_);
        }
        if (logs_to_flush.empty()) return 0;

        std::wstringstream wss;
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now;
        localtime_s(&tm_now, &in_time_t);

        for (const auto& msg : logs_to_flush) {
            wss << L"[" << std::setw(2) << std::setfill(L'0') << tm_now.tm_hour << L":"
                << std::setw(2) << std::setfill(L'0') << tm_now.tm_min << L":"
                << std::setw(2) << std::setfill(L'0') << tm_now.tm_sec << L"] "
                << utf8_to_wstring(msg) << L"\r\n";

            if (msg.find("[BotClient] -> 回复成功") != std::string::npos) {
                self->total_msg_handled_++;
                self->update_status_ui();
            }
        }

        std::wstring wmsg = wss.str();
        int len = GetWindowTextLengthW(self->edit_log_);
        if (len > 150000) {
            SendMessageW(self->edit_log_, EM_SETSEL, 0, 80000);
            SendMessageW(self->edit_log_, EM_REPLACESEL, FALSE, (LPARAM)L"");
            len = GetWindowTextLengthW(self->edit_log_);
        }
        SendMessageW(self->edit_log_, EM_SETSEL, len, len);
        SendMessageW(self->edit_log_, EM_REPLACESEL, FALSE, (LPARAM)wmsg.c_str());
        return 0;
    }

    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        self = (MainWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->init_controls(hwnd);
        return 0;
    }

    case WM_SIZE: {
        if (self) {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            self->relayout_controls(w, h);
        }
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 900;
        mmi->ptMinTrackSize.y = 700;
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        // 1. 顶部状态栏背景
        RECT rcHeader = { 0, 0, rc.right, 50 };
        HBRUSH hbrHeader = CreateSolidBrush(Theme::HeaderBg);
        FillRect(hdc, &rcHeader, hbrHeader);
        DeleteObject(hbrHeader);

        HPEN hPen = CreatePen(PS_SOLID, 1, Theme::HeaderBorder);
        HGDIOBJ oldPen = SelectObject(hdc, hPen);
        MoveToEx(hdc, 0, 50, NULL);
        LineTo(hdc, rc.right, 50);
        SelectObject(hdc, oldPen);
        DeleteObject(hPen);

        // 2. 左侧白色卡片背景 (严密计算尺寸与间隙)
        RECT rcAiCard = { 25, 65, 580, 380 };
        draw_rounded_rect(hdc, rcAiCard, 8, Theme::CardBg, Theme::CardBorder);

        RECT rcObCard = { 25, 395, 580, 635 };
        draw_rounded_rect(hdc, rcObCard, 8, Theme::CardBg, Theme::CardBorder);

        RECT rcPlgCard = { 25, 650, 580, 725 };
        draw_rounded_rect(hdc, rcPlgCard, 8, Theme::CardBg, Theme::CardBorder);

        // 3. 绘制卡片标题
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, Theme::TextMain);
        SelectObject(hdc, self->font_section_);

        RECT rcAiTitle = { 38, 76, 560, 102 };
        DrawTextW(hdc, L"🤖  AI 大模型接口配置", -1, &rcAiTitle, DT_LEFT | DT_TOP | DT_SINGLELINE);

        RECT rcObTitle = { 38, 406, 560, 432 };
        DrawTextW(hdc, L"⚡  OneBot / QQ 接入配置", -1, &rcObTitle, DT_LEFT | DT_TOP | DT_SINGLELINE);

        RECT rcLogTitle = { 605, 126, rc.right - 20, 152 };
        DrawTextW(hdc, L"📋  实时运行日志与交互流", -1, &rcLogTitle, DT_LEFT | DT_TOP | DT_SINGLELINE);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (!self) break;

        // 启停主按钮
        if (dis->CtlID == IDC_BTN_TOGGLE_BOT) {
            COLORREF bg = self->is_bot_running_ ? Theme::DangerRed : Theme::PrimaryBlue;
            if (dis->itemState & ODS_SELECTED) bg = self->is_bot_running_ ? Theme::DangerHover : Theme::PrimaryHover;

            draw_rounded_rect(dis->hDC, dis->rcItem, 8, bg);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, RGB(255, 255, 255));
            SelectObject(dis->hDC, self->font_section_);

            const wchar_t* txt = self->is_bot_running_ ? L"🛑 停止运行中的机器人" : L"🚀 启动 AI 智能机器人";
            DrawTextW(dis->hDC, txt, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }

        // 次级操作按钮
        if (dis->CtlID == IDC_BTN_SAVE_CONFIG || dis->CtlID == IDC_BTN_TEST_API ||
            dis->CtlID == IDC_BTN_CLEAR_LOG || dis->CtlID == IDC_BTN_RELOAD_PLUGINS || dis->CtlID == IDC_BTN_OPEN_PLUGIN_DIR) {
            COLORREF bg = Theme::CardBg;
            COLORREF border = Theme::CardBorder;
            COLORREF txtColor = Theme::TextMain;

            if (dis->CtlID == IDC_BTN_SAVE_CONFIG) {
                txtColor = Theme::PrimaryBlue;
            } else if (dis->CtlID == IDC_BTN_TEST_API) {
                txtColor = RGB(16, 185, 129);
            }

            if (dis->itemState & ODS_SELECTED) bg = RGB(229, 231, 235);

            draw_rounded_rect(dis->hDC, dis->rcItem, 6, bg, border);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, txtColor);
            SelectObject(dis->hDC, self->font_body_bold_);

            wchar_t text[64];
            GetWindowTextW(dis->hwndItem, text, 64);
            DrawTextW(dis->hDC, text, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        if (!self) break;

        switch (wmId) {
        case IDC_BTN_TOGGLE_BOT:
            self->toggle_bot_state();
            break;
        case IDC_BTN_SAVE_CONFIG:
            self->save_ui_to_config();
            MessageBoxW(hwnd, L"✅ 配置已成功保存至 config.json！", L"保存成功", MB_OK | MB_ICONINFORMATION);
            break;
        case IDC_BTN_TEST_API:
            self->test_api_connection();
            break;
        case IDC_BTN_CLEAR_LOG:
            SetWindowTextW(self->edit_log_, L"");
            self->log("✨ 日志已清空。");
            break;
        case IDC_BTN_RELOAD_PLUGINS:
            PluginManager::get_instance().load_plugins("plugins");
            self->refresh_plugin_ui();
            break;
        case IDC_BTN_OPEN_PLUGIN_DIR:
            CreateDirectoryW(L"plugins", NULL);
            ShellExecuteW(NULL, L"open", L"plugins", NULL, NULL, SW_SHOW);
            break;
        }
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        HWND hwndStatic = (HWND)lParam;

        if (self && (hwndStatic == self->label_status_badge_ || hwndStatic == self->label_active_model_ || hwndStatic == self->label_reply_count_)) {
            SetBkMode(hdcStatic, TRANSPARENT);
            if (hwndStatic == self->label_status_badge_) {
                SetTextColor(hdcStatic, self->is_bot_running_ ? Theme::SuccessGreen : Theme::DangerRed);
            } else if (hwndStatic == self->label_active_model_) {
                SetTextColor(hdcStatic, Theme::PrimaryBlue);
            } else {
                SetTextColor(hdcStatic, Theme::TextMain);
            }
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }

        SetBkMode(hdcStatic, TRANSPARENT);
        SetTextColor(hdcStatic, Theme::TextMain);
        return (LRESULT)self->brush_win_bg_;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wParam;
        HWND hwndEdit = (HWND)lParam;
        if (self && hwndEdit == self->edit_log_) {
            SetBkColor(hdcEdit, Theme::LogBg);
            SetTextColor(hdcEdit, Theme::LogText);
            return (LRESULT)self->brush_log_bg_;
        }
        SetBkColor(hdcEdit, Theme::InputBg);
        SetTextColor(hdcEdit, Theme::TextMain);
        return (LRESULT)self->brush_input_bg_;
    }

    case WM_DESTROY: {
        if (self && self->is_bot_running_ && self->bot_client_) {
            self->bot_client_->stop();
        }
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
