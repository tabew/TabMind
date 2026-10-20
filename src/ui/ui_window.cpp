#include "ui_window.hpp"
#include "ui_dialogs.hpp"
#include "ui_theme.hpp"
#include "plugin_manager.hpp"
#include <richedit.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <shellapi.h>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "msimg32.lib")

// ---------------------------------------------------------------------------
// 控件 ID
// ---------------------------------------------------------------------------
#define IDC_BTN_TOGGLE_BOT       1001
#define IDC_BTN_SAVE_CONFIG      1002
#define IDC_BTN_TEST_API         1003
#define IDC_BTN_CLEAR_LOG        1004
#define IDC_BTN_RELOAD_PLUGINS   1005
#define IDC_BTN_OPEN_PLUGIN_DIR  1006
#define IDC_BTN_ADVANCED         1007
#define IDC_BTN_PLUGIN_MGR       1008
#define IDC_BTN_HELP             1009
#define IDC_BTN_RESET_LAYOUT     1010
#define IDC_BTN_THEME            1011
#define IDC_BTN_LAYOUT_MENU      1012

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

// 布局菜单项
#define IDM_RESET_LAYOUT         2001
#define IDM_TOGGLE_LOCK          2002
#define IDM_MOD_BASE             2100   // + 模块序号

// ---------------------------------------------------------------------------
// 布局常量
// ---------------------------------------------------------------------------
namespace Lay {
    const int HeaderH   = 62;
    const int ModTitleH = 38;   // 模块标题栏（拖拽区）
    const int GripSize  = 18;   // 右下角缩放手柄
    const int Pad       = 20;
    const int Gap       = 14;
    const int Content   = 16;   // 模块内容左右内边距
    const int Top       = 74;   // 模块区起始 Y
}

// ---------------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------------
static std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstr[0], size);
    return wstr;
}

static std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0], size, NULL, NULL);
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

// 日志中的 emoji 在 RichEdit 里会显示成方块，这里剔除（颜色已表达语义）
static void strip_emoji(std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() &&
            s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            ++i;
            continue;
        }
        if ((c >= 0x2600 && c <= 0x27BF) || (c >= 0x2B00 && c <= 0x2BFF) ||
            c == 0xFE0F || c == 0xFE0E || c == 0x200D ||
            c == 0x2139 || c == 0x2049 || c == 0x203C) {
            continue;
        }
        out.push_back(c);
    }
    std::wstring cleaned;
    bool prev_space = false;
    for (wchar_t c : out) {
        bool sp = (c == L' ');
        if (sp && prev_space) continue;
        cleaned.push_back(c);
        prev_space = sp;
    }
    size_t b = cleaned.find_first_not_of(L' ');
    if (b == std::wstring::npos) cleaned.clear();
    else if (b > 0) cleaned = cleaned.substr(b);
    s.swap(cleaned);
}

static COLORREF log_color_for(const std::string& raw) {
    const ThemePalette& t = theme();
    auto has = [&](const char* k) { return raw.find(k) != std::string::npos; };
    if (has("❌") || has("失败") || has("错误") || has("异常") || has("FAIL")) return t.log_err;
    if (has("⚠") || has("未配置") || has("警告")) return t.log_warn;
    if (has("✅") || has("√") || has("已上线") || has("已就绪") || has("通过")) return t.log_ok;
    if (has("AI") || has("回复") || has("模型")) return t.log_ai;
    return t.log_info;
}

// ---------------------------------------------------------------------------
// 构造 / GDI
// ---------------------------------------------------------------------------
MainWindow::MainWindow() {}

MainWindow::~MainWindow() {
    HFONT fonts[] = { font_brand_, font_h2_, font_body_, font_body_bold_, font_small_, font_log_ };
    for (HFONT f : fonts) if (f) DeleteObject(f);
    HBRUSH brushes[] = { brush_win_bg_, brush_card_bg_, brush_field_bg_, brush_log_bg_ };
    for (HBRUSH b : brushes) if (b) DeleteObject(b);
}

MainWindow& MainWindow::get_instance() {
    static MainWindow instance;
    return instance;
}

void MainWindow::init_gdi_resources() {
    auto mk = [](int h, int weight, const wchar_t* face) {
        return CreateFontW(-h, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
    };
    font_brand_     = mk(20, FW_BOLD,   L"Microsoft YaHei UI");
    font_h2_        = mk(15, FW_BOLD,   L"Microsoft YaHei UI");
    font_body_      = mk(14, FW_NORMAL, L"Microsoft YaHei UI");
    font_body_bold_ = mk(14, FW_BOLD,   L"Microsoft YaHei UI");
    font_small_     = mk(12, FW_NORMAL, L"Microsoft YaHei UI");
    font_log_       = mk(13, FW_NORMAL, L"Microsoft YaHei UI");
}

void MainWindow::apply_theme(int index, bool persist) {
    set_theme(index);
    applied_theme_ = theme_index();
    const ThemePalette& t = theme();

    if (brush_win_bg_) DeleteObject(brush_win_bg_);
    if (brush_card_bg_) DeleteObject(brush_card_bg_);
    if (brush_field_bg_) DeleteObject(brush_field_bg_);
    if (brush_log_bg_) DeleteObject(brush_log_bg_);
    brush_win_bg_   = CreateSolidBrush(t.win_bg);
    brush_card_bg_  = CreateSolidBrush(t.card_bg);
    brush_field_bg_ = CreateSolidBrush(t.field_bg);
    brush_log_bg_   = CreateSolidBrush(t.log_bg);

    if (edit_log_ && log_is_riched_) {
        SendMessageW(edit_log_, EM_SETBKGNDCOLOR, 0, (LPARAM)t.log_bg);
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
        cf.crTextColor = t.log_text;
        cf.yHeight = 200;
        lstrcpynW(cf.szFaceName, L"Microsoft YaHei UI", LF_FACESIZE);
        SendMessageW(edit_log_, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    }

    if (btn_theme_) {
        std::wstring label = std::wstring(L"主题 · ") + theme().name;
        SetWindowTextW(btn_theme_, label.c_str());
    }

    if (persist) {
        config_.ui.theme = theme_index();
        config_.save_to_file("config.json");
    }

    // 原生 EDIT / COMBOBOX 不理会自绘配色，用暗色视觉样式让它们跟上主题
    for (int i = 0; i < MOD_COUNT; ++i) {
        for (HWND h : mod_ctl_[i]) {
            if (!h) continue;
            SetWindowTheme(h, t.dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
            SetWindowPos(h, NULL, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            InvalidateRect(h, NULL, TRUE);
        }
    }

    if (h_wnd_) {
        RedrawWindow(h_wnd_, NULL, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
}

LRESULT CALLBACK MainWindow::button_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                  UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    MainWindow* self = (MainWindow*)dwRefData;
    switch (uMsg) {
    case WM_ERASEBKGND: {
        // 自绘按钮圆角外会露出控件底色，必须先刷成卡片色
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (self && self->brush_card_bg_) FillRect(hdc, &rc, self->brush_card_bg_);
        return 1;
    }
    case WM_MOUSEMOVE:
        if (self) {
            int id = (int)uIdSubclass;
            if (self->hovered_.insert(id).second) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        break;
    case WM_MOUSELEAVE:
        if (self) {
            self->hovered_.erase((int)uIdSubclass);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        break;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// 创建窗口
// ---------------------------------------------------------------------------
bool MainWindow::create_and_show(HINSTANCE hInstance, int nCmdShow) {
    h_instance_ = hInstance;

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    init_gdi_resources();

    config_ = AppConfig::load_from_file("config.json");
    apply_theme(config_.ui.theme, false);

    const wchar_t CLASS_NAME[] = L"TabMind_Resizable_Window";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWindow::window_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = brush_win_bg_;
    RegisterClassW(&wc);

    RECT wa = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int win_w = 1240, win_h = 820;
    int max_w = (wa.right - wa.left) - 30;
    int max_h = (wa.bottom - wa.top) - 30;
    if (win_w > max_w) win_w = max_w;
    if (win_h > max_h) win_h = max_h;
    if (win_w < 980) win_w = 980;
    if (win_h < 660) win_h = 660;
    int pos_x = wa.left + ((wa.right - wa.left) - win_w) / 2;
    int pos_y = wa.top + ((wa.bottom - wa.top) - win_h) / 2;

    h_wnd_ = CreateWindowExW(0, CLASS_NAME, L"TabMind · 智能聊天机器人管理终端",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             pos_x, pos_y, win_w, win_h, NULL, NULL, hInstance, this);
    if (!h_wnd_) return false;

    load_config_to_ui();
    update_status_ui();

    PluginManager::get_instance().load_plugins("plugins");
    refresh_plugin_ui();

    ShowWindow(h_wnd_, nCmdShow);
    UpdateWindow(h_wnd_);

    log("管理终端已就绪");
    log("OneBot 服务端: " + config_.onebot.ws_url);

    if (!config_.ai.api_key.empty()) {
        log("正在连接 OneBot WebSocket ...");
        bot_client_ = std::make_unique<BotClient>(config_);
        bot_client_->start();
        is_bot_running_ = true;
        update_status_ui();
        log("机器人已上线，正在监听消息");
    } else {
        log("未配置 API Key，请在「AI 接口配置」模块填写后启动");
    }
    return true;
}

// ---------------------------------------------------------------------------
// 控件创建
// ---------------------------------------------------------------------------
void MainWindow::init_controls(HWND hwnd) {
    auto mk_label = [&](const wchar_t* text, HFONT font) {
        HWND h = CreateWindowW(L"STATIC", text, WS_CHILD | SS_LEFT,
                               -2000, -2000, 10, 10, hwnd, NULL, h_instance_, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
        return h;
    };
    auto mk_edit = [&](int id, const std::wstring& text, DWORD style) {
        HWND e = CreateWindowExW(0, L"EDIT", text.c_str(),
                                 WS_CHILD | ES_AUTOHSCROLL | style,
                                 -2000, -2000, 10, 10, hwnd, (HMENU)(INT_PTR)id, h_instance_, NULL);
        SendMessageW(e, WM_SETFONT, (WPARAM)font_body_, TRUE);
        SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
        return e;
    };
    auto mk_button = [&](int id, const wchar_t* text) {
        HWND b = CreateWindowW(L"BUTTON", text, WS_CHILD | BS_OWNERDRAW,
                               -2000, -2000, 10, 10, hwnd, (HMENU)(INT_PTR)id, h_instance_, NULL);
        SetWindowSubclass(b, MainWindow::button_subclass_proc, id, (DWORD_PTR)this);
        return b;
    };

    // ---------- 顶栏 ----------
    btn_theme_ = mk_button(IDC_BTN_THEME, L"主题");
    btn_layout_ = mk_button(IDC_BTN_LAYOUT_MENU, L"布局");
    SetWindowTextW(btn_theme_, (std::wstring(L"主题 · ") + theme().name).c_str());

    // ---------- AI 模块 ----------
    HWND l;
    l = mk_label(L"API 接口地址 (Base URL)", font_small_);      mod_ctl_[MOD_AI].push_back(l);
    edit_base_url_ = mk_edit(IDC_EDIT_BASE_URL, L"", 0);        mod_ctl_[MOD_AI].push_back(edit_base_url_);
    l = mk_label(L"API 密钥 (API Key)", font_small_);           mod_ctl_[MOD_AI].push_back(l);
    edit_api_key_ = mk_edit(IDC_EDIT_API_KEY, L"", ES_PASSWORD); mod_ctl_[MOD_AI].push_back(edit_api_key_);
    l = mk_label(L"模型", font_small_);                         mod_ctl_[MOD_AI].push_back(l);
    l = mk_label(L"Max Tokens", font_small_);                   mod_ctl_[MOD_AI].push_back(l);
    l = mk_label(L"上下文轮数", font_small_);                    mod_ctl_[MOD_AI].push_back(l);

    combo_model_ = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                                 -2000, -2000, 10, 200, hwnd, (HMENU)IDC_COMBO_MODEL, h_instance_, NULL);
    SendMessageW(combo_model_, WM_SETFONT, (WPARAM)font_body_, TRUE);
    {
        const wchar_t* models[] = { L"deepseek-chat", L"deepseek-reasoner", L"deepseek-v4-flash",
                                    L"gpt-4o-mini", L"gpt-4o" };
        for (const wchar_t* m : models) {
            SendMessageW(combo_model_, CB_ADDSTRING, 0, (LPARAM)m);
        }
    }
    mod_ctl_[MOD_AI].push_back(combo_model_);

    edit_max_tokens_ = mk_edit(IDC_EDIT_MAX_TOKENS, L"", ES_NUMBER);
    mod_ctl_[MOD_AI].push_back(edit_max_tokens_);
    edit_history_rounds_ = mk_edit(IDC_EDIT_HISTORY_ROUNDS, L"", ES_NUMBER);
    mod_ctl_[MOD_AI].push_back(edit_history_rounds_);

    l = mk_label(L"系统提示词 / 人设", font_small_);             mod_ctl_[MOD_AI].push_back(l);
    edit_system_prompt_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        -2000, -2000, 10, 10, hwnd, (HMENU)IDC_EDIT_SYSTEM_PROMPT, h_instance_, NULL);
    SendMessageW(edit_system_prompt_, WM_SETFONT, (WPARAM)font_body_, TRUE);
    SendMessageW(edit_system_prompt_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
    mod_ctl_[MOD_AI].push_back(edit_system_prompt_);

    // ---------- QQ 模块 ----------
    l = mk_label(L"OneBot WebSocket 地址", font_small_);         mod_ctl_[MOD_QQ].push_back(l);
    edit_ws_url_ = mk_edit(IDC_EDIT_WS_URL, L"", 0);             mod_ctl_[MOD_QQ].push_back(edit_ws_url_);
    l = mk_label(L"机器人 QQ 号", font_small_);                   mod_ctl_[MOD_QQ].push_back(l);
    l = mk_label(L"Access Token（无则留空）", font_small_);       mod_ctl_[MOD_QQ].push_back(l);
    edit_bot_qq_ = mk_edit(IDC_EDIT_BOT_QQ, L"", ES_NUMBER);      mod_ctl_[MOD_QQ].push_back(edit_bot_qq_);
    edit_access_token_ = mk_edit(IDC_EDIT_ACCESS_TOKEN, L"", 0);  mod_ctl_[MOD_QQ].push_back(edit_access_token_);
    l = mk_label(L"触发前缀词（逗号分隔）", font_small_);          mod_ctl_[MOD_QQ].push_back(l);
    edit_prefixes_ = mk_edit(IDC_EDIT_PREFIXES, L"", 0);         mod_ctl_[MOD_QQ].push_back(edit_prefixes_);

    chk_group_require_at_ = CreateWindowW(L"BUTTON", L"群聊必须 @机器人 或命中前缀才响应",
        WS_CHILD | BS_AUTOCHECKBOX, -2000, -2000, 10, 10, hwnd, (HMENU)IDC_CHK_REQUIRE_AT, h_instance_, NULL);
    SendMessageW(chk_group_require_at_, WM_SETFONT, (WPARAM)font_body_, TRUE);
    mod_ctl_[MOD_QQ].push_back(chk_group_require_at_);

    l = mk_label(L"免 @ 插件（逗号分隔）", font_small_);           mod_ctl_[MOD_QQ].push_back(l);
    edit_at_exempt_ = mk_edit(IDC_EDIT_AT_EXEMPT, L"", 0);       mod_ctl_[MOD_QQ].push_back(edit_at_exempt_);

    // ---------- 插件 ----------
    label_plugin_summary_ = mk_label(L"正在加载插件...", font_body_);
    mod_ctl_[MOD_PLUGIN].push_back(label_plugin_summary_);
    btn_reload_plugins_ = mk_button(IDC_BTN_RELOAD_PLUGINS, L"重载插件");
    mod_ctl_[MOD_PLUGIN].push_back(btn_reload_plugins_);
    btn_open_plugin_dir_ = mk_button(IDC_BTN_OPEN_PLUGIN_DIR, L"插件目录");
    mod_ctl_[MOD_PLUGIN].push_back(btn_open_plugin_dir_);
    btn_plugin_mgr_ = mk_button(IDC_BTN_PLUGIN_MGR, L"插件管理");
    mod_ctl_[MOD_PLUGIN].push_back(btn_plugin_mgr_);

    // ---------- 快捷操作 ----------
    btn_toggle_bot_ = mk_button(IDC_BTN_TOGGLE_BOT, L"启动机器人");
    mod_ctl_[MOD_ACTION].push_back(btn_toggle_bot_);
    btn_save_config_ = mk_button(IDC_BTN_SAVE_CONFIG, L"保存配置");
    mod_ctl_[MOD_ACTION].push_back(btn_save_config_);
    btn_test_api_ = mk_button(IDC_BTN_TEST_API, L"测试接口");
    mod_ctl_[MOD_ACTION].push_back(btn_test_api_);
    btn_advanced_ = mk_button(IDC_BTN_ADVANCED, L"高级设置");
    mod_ctl_[MOD_ACTION].push_back(btn_advanced_);
    btn_help_ = mk_button(IDC_BTN_HELP, L"使用说明");
    mod_ctl_[MOD_ACTION].push_back(btn_help_);
    btn_reset_layout_ = mk_button(IDC_BTN_RESET_LAYOUT, L"重置布局");
    mod_ctl_[MOD_ACTION].push_back(btn_reset_layout_);

    // ---------- 日志 ----------
    LoadLibraryW(L"Msftedit.dll");
    log_is_riched_ = (GetModuleHandleW(L"Msftedit.dll") != nullptr);
    const wchar_t* log_cls = log_is_riched_ ? L"RICHEDIT50W" : L"EDIT";
    edit_log_ = CreateWindowExW(0, log_cls, L"",
        WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | ES_NOHIDESEL,
        -2000, -2000, 10, 10, hwnd, (HMENU)IDC_EDIT_LOG, h_instance_, NULL);
    SendMessageW(edit_log_, EM_LIMITTEXT, 0, 0);
    SendMessageW(edit_log_, WM_SETFONT, (WPARAM)font_log_, TRUE);
    if (log_is_riched_) {
        SendMessageW(edit_log_, EM_SETBKGNDCOLOR, 0, (LPARAM)theme().log_bg);
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
        cf.crTextColor = theme().log_text;
        cf.yHeight = 200;
        lstrcpynW(cf.szFaceName, L"Microsoft YaHei UI", LF_FACESIZE);
        SendMessageW(edit_log_, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    }
    mod_ctl_[MOD_LOG].push_back(edit_log_);
    btn_clear_log_ = mk_button(IDC_BTN_CLEAR_LOG, L"清空");
    mod_ctl_[MOD_LOG].push_back(btn_clear_log_);

    // ---------- 初始布局 ----------
    mod_order_.clear();
    for (int i = 0; i < MOD_COUNT; ++i) mod_order_.push_back(i);

    RECT rc;
    GetClientRect(hwnd, &rc);
    if (config_.ui.modules.size() == (size_t)MOD_COUNT) {
        load_layout();
    } else {
        default_layout(rc.right, rc.bottom);
    }
    relayout_all();
}

// ---------------------------------------------------------------------------
// 布局
// ---------------------------------------------------------------------------
void MainWindow::default_layout(int width, int height) {
    const int pad = Lay::Pad, gap = Lay::Gap, top = Lay::Top;
    int avail_w = width - pad * 2 - gap * 2;
    if (avail_w < 600) avail_w = 600;
    int c1 = avail_w * 34 / 100;
    int c2 = avail_w * 30 / 100;
    int c3 = avail_w - c1 - c2;
    int x1 = pad, x2 = x1 + c1 + gap, x3 = x2 + c2 + gap;
    int avail_h = height - top - pad;
    if (avail_h < 420) avail_h = 420;

    auto set = [&](int id, int x, int y, int w, int h) {
        mod_rc_[id] = { x, y, x + w, y + h };
        mod_visible_[id] = true;
    };
    set(MOD_STATUS, x1, top, c1, 104);
    set(MOD_AI,     x1, top + 118, c1, std::max(300, avail_h - 118));
    set(MOD_PLUGIN, x2, top, c2, 240);
    set(MOD_QQ,     x2, top + 254, c2, std::max(300, avail_h - 254));
    set(MOD_ACTION, x3, top, c3, 268);
    set(MOD_LOG,    x3, top + 282, c3, std::max(200, avail_h - 282));
}

void MainWindow::load_layout() {
    for (int i = 0; i < MOD_COUNT; ++i) {
        const UILayoutItem& it = config_.ui.modules[i];
        mod_rc_[i] = { it.x, it.y, it.x + it.w, it.y + it.h };
        mod_visible_[i] = it.visible;
    }
}

void MainWindow::save_layout() {
    config_.ui.modules.clear();
    for (int i = 0; i < MOD_COUNT; ++i) {
        UILayoutItem it;
        it.x = mod_rc_[i].left;
        it.y = mod_rc_[i].top;
        it.w = mod_rc_[i].right - mod_rc_[i].left;
        it.h = mod_rc_[i].bottom - mod_rc_[i].top;
        it.visible = mod_visible_[i];
        config_.ui.modules.push_back(it);
    }
    config_.ui.theme = theme_index();
    config_.save_to_file("config.json");
}

void MainWindow::relayout_module(int id) {
    const RECT& rc = mod_rc_[id];
    bool vis = mod_visible_[id];
    for (HWND h : mod_ctl_[id]) ShowWindow(h, vis ? SW_SHOW : SW_HIDE);
    if (!vis) return;

    int L = rc.left + Lay::Content;
    int W = (rc.right - Lay::Content) - L;
    int top = rc.top + Lay::ModTitleH;
    if (W < 80) W = 80;

    auto place = [](HWND h, int x, int y, int w, int hgt) {
        if (h) SetWindowPos(h, NULL, x, y, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
    };

    if (id == MOD_AI) {
        // 控件顺序：0 label(base) 2 label(key) 4 label(模型) 5 label(tokens) 6 label(轮数)
        //           10 label(prompt)；输入框直接用成员 HWND，避免下标错位
        auto& c = mod_ctl_[MOD_AI];
        const int rowH = 30, gapY = 26;
        int y = top + 2;

        place(c[0], L, y, W, 16);
        place(edit_base_url_, L, y + 17, W, rowH);
        y += rowH + 17 + gapY;

        place(c[2], L, y, W, 16);
        place(edit_api_key_, L, y + 17, W, rowH);
        y += rowH + 17 + gapY;

        int cModel = W * 50 / 100;
        int cTok = W * 21 / 100;
        int cRound = W - cModel - cTok - 16;
        if (cRound < 46) cRound = 46;
        place(c[4], L, y, cModel, 16);
        place(combo_model_, L, y + 17, cModel, 220);
        place(c[5], L + cModel + 8, y, cTok, 16);
        place(edit_max_tokens_, L + cModel + 8, y + 17, cTok, rowH);
        place(c[6], L + cModel + cTok + 16, y, cRound, 16);
        place(edit_history_rounds_, L + cModel + cTok + 16, y + 17, cRound, rowH);
        y += rowH + 17 + gapY;

        place(c[10], L, y, W, 16);
        int promptH = rc.bottom - 12 - (y + 17);
        if (promptH < 60) promptH = 60;
        place(edit_system_prompt_, L, y + 17, W, promptH);
    } else if (id == MOD_QQ) {
        // 顺序：0 label(ws) 2 label(bot) 3 label(token) 6 label(prefix) 9 label(exempt)
        auto& c = mod_ctl_[MOD_QQ];
        const int rowH = 30, gapY = 24;
        int y = top + 2;

        place(c[0], L, y, W, 16);
        place(edit_ws_url_, L, y + 17, W, rowH);
        y += rowH + 17 + gapY;

        int cBot = W * 40 / 100;
        place(c[2], L, y, cBot, 16);
        place(c[3], L + cBot + 8, y, W - cBot - 8, 16);
        place(edit_bot_qq_, L, y + 17, cBot, rowH);
        place(edit_access_token_, L + cBot + 8, y + 17, W - cBot - 8, rowH);
        y += rowH + 17 + gapY;

        place(c[6], L, y, W, 16);
        place(edit_prefixes_, L, y + 17, W, rowH);
        y += rowH + 17 + gapY;

        place(chk_group_require_at_, L, y, W, 22);
        y += 30;
        place(c[9], L, y, W, 16);
        place(edit_at_exempt_, L, y + 17, W, rowH);
    } else if (id == MOD_PLUGIN) {
        place(label_plugin_summary_, L, top + 2, W, 38);
        int y = top + 46;
        int bw = (W - 16) / 3;
        if (bw < 68) bw = 68;
        place(btn_reload_plugins_, L, y, bw, 32);
        place(btn_open_plugin_dir_, L + bw + 8, y, bw, 32);
        place(btn_plugin_mgr_, L + (bw + 8) * 2, y, bw, 32);
    } else if (id == MOD_ACTION) {
        int y = top + 4;
        place(btn_toggle_bot_, L, y, W, 44);
        y += 52;
        int half = (W - 8) / 2;
        place(btn_save_config_, L, y, half, 34);
        place(btn_test_api_, L + half + 8, y, half, 34);
        y += 42;
        place(btn_advanced_, L, y, half, 34);
        place(btn_help_, L + half + 8, y, half, 34);
        y += 42;
        place(btn_reset_layout_, L, y, W, 34);
    } else if (id == MOD_LOG) {
        place(btn_clear_log_, rc.right - Lay::Content - 56, rc.top + 7, 56, 24);
        int y = top + 2;
        int h = rc.bottom - 12 - y;
        if (h < 60) h = 60;
        place(edit_log_, L - 4, y, W + 8, h);
        if (log_is_riched_) {
            RECT fr = { 12, 8, (W + 8) - 12, h - 8 };
            SendMessageW(edit_log_, EM_SETRECT, 0, (LPARAM)&fr);
        }
    }
}

void MainWindow::relayout_all() {
    for (int i = 0; i < MOD_COUNT; ++i) relayout_module(i);
    if (h_wnd_) InvalidateRect(h_wnd_, NULL, TRUE);
}

int MainWindow::module_at(POINT pt, bool& on_grip) const {
    on_grip = false;
    for (int k = (int)mod_order_.size() - 1; k >= 0; --k) {
        int i = mod_order_[k];
        if (!mod_visible_[i]) continue;
        const RECT& r = mod_rc_[i];
        if (pt.x < r.left || pt.x >= r.right || pt.y < r.top || pt.y >= r.bottom) continue;
        if (pt.x >= r.right - Lay::GripSize && pt.y >= r.bottom - Lay::GripSize) {
            on_grip = true;
            return i;
        }
        if (pt.y < r.top + Lay::ModTitleH) return i;
        return -1;   // 卡片主体交给控件
    }
    return -1;
}

const wchar_t* MainWindow::module_title(int id) {
    switch (id) {
    case MOD_STATUS: return L"运行状态";
    case MOD_AI:     return L"AI 接口配置";
    case MOD_QQ:     return L"OneBot / QQ 接入";
    case MOD_PLUGIN: return L"扩展插件";
    case MOD_ACTION: return L"快捷操作";
    case MOD_LOG:    return L"运行日志";
    default:         return L"";
    }
}

void MainWindow::paint_module(HDC hdc, int id) {
    const RECT& rc = mod_rc_[id];
    const ThemePalette& t = theme();

    ui_draw_card(hdc, rc, 12);

    HGDIOBJ old = SelectObject(hdc, font_h2_);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, t.text_main);
    RECT rcTitle = { rc.left + Lay::Content, rc.top + 9, rc.right - Lay::Content - 60, rc.top + 32 };
    DrawTextW(hdc, module_title(id), -1, &rcTitle, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

    // 可拖动提示：三个圆点
    if (!config_.ui.locked) {
        HBRUSH dot = CreateSolidBrush(t.text_faint);
        HGDIOBJ ob = SelectObject(hdc, dot);
        HGDIOBJ opn = SelectObject(hdc, GetStockObject(NULL_PEN));
        for (int k = 0; k < 3; ++k) {
            int cx = rc.right - 74 + k * 7;
            Ellipse(hdc, cx, rc.top + 17, cx + 3, rc.top + 20);
        }
        SelectObject(hdc, ob);
        SelectObject(hdc, opn);
        DeleteObject(dot);
    }

    if (id == MOD_STATUS) {
        int L = rc.left + Lay::Content;
        int W = (rc.right - Lay::Content) - L;
        int y = rc.top + Lay::ModTitleH + 2;

        int w1 = W * 30 / 100, w2 = W * 40 / 100, w3 = W - w1 - w2 - 16;
        if (w3 < 60) w3 = 60;
        RECT r1 = { L, y, L + w1, y + 30 };
        RECT r2 = { L + w1 + 8, y, L + w1 + 8 + w2, y + 30 };
        RECT r3 = { L + w1 + w2 + 16, y, L + w1 + w2 + 16 + w3, y + 30 };

        bool run = is_bot_running_;
        ui_draw_pill(hdc, r1, run ? t.success_soft : t.danger_soft,
                     run ? t.success_text : t.danger,
                     run ? L"● 运行中" : L"● 已停止", font_small_);
        ui_draw_pill(hdc, r2, t.primary_soft, t.primary,
                     L"模型 " + utf8_to_wstring(config_.ai.model), font_small_);
        ui_draw_pill(hdc, r3, t.field_bg, t.text_main,
                     L"已回复 " + std::to_wstring(total_msg_handled_), font_small_);

        SelectObject(hdc, font_small_);
        SetTextColor(hdc, t.text_faint);
        RECT rcHint = { L, y + 32, rc.right - Lay::Content, y + 50 };
        DrawTextW(hdc, config_.ui.locked ? L"布局已锁定（「布局」菜单可解锁）"
                                         : L"拖动标题栏移动 · 拖右下角缩放",
                  -1, &rcHint, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else if (id == MOD_PLUGIN) {
        int L = rc.left + Lay::Content;
        int W = (rc.right - Lay::Content) - L;
        int list_top = rc.top + Lay::ModTitleH + 86;
        int row_h = 26;
        int max_rows = (rc.bottom - 14 - 18 - list_top) / row_h;
        if (max_rows < 1) max_rows = 1;
        auto plugins = PluginManager::get_instance().get_plugin_list();

        SelectObject(hdc, font_small_);
        SetBkMode(hdc, TRANSPARENT);
        if (plugins.empty()) {
            SetTextColor(hdc, t.text_faint);
            RECT rcE = { L, list_top, L + W, list_top + 20 };
            DrawTextW(hdc, L"暂无插件 · 把 DLL 放进 plugins 目录", -1, &rcE,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            int shown = 0;
            for (const auto& pl : plugins) {
                if (shown >= max_rows || shown >= 6) break;
                int y = list_top + shown * row_h;
                RECT rcRow = { L, y, L + W, y + row_h - 4 };
                ui_fill_round(hdc, rcRow, 7, t.field_bg);

                HBRUSH dot = CreateSolidBrush(pl.enabled ? t.success : t.text_faint);
                HGDIOBJ ob = SelectObject(hdc, dot);
                HGDIOBJ opn = SelectObject(hdc, GetStockObject(NULL_PEN));
                Ellipse(hdc, L + 9, y + 8, L + 15, y + 14);
                SelectObject(hdc, ob);
                SelectObject(hdc, opn);
                DeleteObject(dot);

                SelectObject(hdc, font_body_);
                SetTextColor(hdc, pl.enabled ? t.text_main : t.text_sub);
                RECT rcName = { L + 22, y + 2, L + W - 66, y + row_h - 4 };
                DrawTextW(hdc, utf8_to_wstring(pl.name).c_str(), -1, &rcName,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                SelectObject(hdc, font_small_);
                SetTextColor(hdc, t.text_faint);
                RECT rcVer = { L + W - 64, y + 2, L + W - 8, y + row_h - 4 };
                DrawTextW(hdc, utf8_to_wstring("v" + pl.version).c_str(), -1, &rcVer,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                ++shown;
            }
            if ((int)plugins.size() > shown) {
                RECT rcMore = { L, list_top + shown * row_h, L + W, list_top + shown * row_h + 18 };
                SetTextColor(hdc, t.text_faint);
                std::wstring more = L"还有 " + std::to_wstring(plugins.size() - shown) + L" 个插件";
                DrawTextW(hdc, more.c_str(), -1, &rcMore, DT_LEFT | DT_TOP | DT_SINGLELINE);
            }
        }
    } else if (id == MOD_ACTION) {
        RECT rcHint = { rc.left + Lay::Content, rc.bottom - 24, rc.right - Lay::Content, rc.bottom - 8 };
        SelectObject(hdc, font_small_);
        SetTextColor(hdc, t.text_faint);
        DrawTextW(hdc, L"模块可自由拖动拼接，布局自动保存", -1, &rcHint,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, old);
}

void MainWindow::show_layout_menu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_RESET_LAYOUT, L"重置为默认布局");
    AppendMenuW(menu, MF_STRING | (config_.ui.locked ? MF_CHECKED : 0), IDM_TOGGLE_LOCK,
                config_.ui.locked ? L"解锁布局（当前已锁定）" : L"锁定布局（防止误拖）");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < MOD_COUNT; ++i) {
        AppendMenuW(menu, MF_STRING | (mod_visible_[i] ? MF_CHECKED : 0),
                    IDM_MOD_BASE + i, module_title(i));
    }

    RECT r;
    GetWindowRect(btn_layout_, &r);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                             r.left, r.bottom + 4, 0, h_wnd_, NULL);
    DestroyMenu(menu);

    if (cmd == IDM_RESET_LAYOUT) {
        RECT rc;
        GetClientRect(h_wnd_, &rc);
        default_layout(rc.right, rc.bottom);
        relayout_all();
        save_layout();
        log("布局已重置为默认");
    } else if (cmd == IDM_TOGGLE_LOCK) {
        config_.ui.locked = !config_.ui.locked;
        save_layout();
        relayout_all();
    } else if (cmd >= IDM_MOD_BASE && cmd < IDM_MOD_BASE + MOD_COUNT) {
        int idx = cmd - IDM_MOD_BASE;
        mod_visible_[idx] = !mod_visible_[idx];
        relayout_all();
        save_layout();
    }
}

// ---------------------------------------------------------------------------
// 配置 <-> 界面
// ---------------------------------------------------------------------------
void MainWindow::load_config_to_ui() {
    set_window_text_u8(edit_api_key_, config_.ai.api_key);
    set_window_text_u8(edit_base_url_, config_.ai.base_url);

    {
        std::wstring want = utf8_to_wstring(config_.ai.model);
        int idx = (int)SendMessageW(combo_model_, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)want.c_str());
        if (idx == CB_ERR && !want.empty()) {
            SendMessageW(combo_model_, CB_ADDSTRING, 0, (LPARAM)want.c_str());
            idx = (int)SendMessageW(combo_model_, CB_GETCOUNT, 0, 0) - 1;
        }
        if (idx != CB_ERR) SendMessageW(combo_model_, CB_SETCURSEL, idx, 0);
    }

    set_window_text_u8(edit_system_prompt_, config_.ai.system_prompt);
    set_window_text_u8(edit_max_tokens_, std::to_string(config_.ai.max_tokens));
    set_window_text_u8(edit_history_rounds_, std::to_string(config_.ai.max_history_rounds));

    set_window_text_u8(edit_ws_url_, config_.onebot.ws_url);
    set_window_text_u8(edit_bot_qq_, std::to_string(config_.onebot.bot_qq));
    set_window_text_u8(edit_access_token_, config_.onebot.access_token);

    SendMessageW(chk_group_require_at_, BM_SETCHECK,
                 config_.onebot.group_require_at ? BST_CHECKED : BST_UNCHECKED, 0);

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

    try { config_.ai.max_tokens = std::stoi(get_window_text_u8(edit_max_tokens_)); }
    catch (...) { config_.ai.max_tokens = 1000; }
    try { config_.ai.max_history_rounds = std::stoi(get_window_text_u8(edit_history_rounds_)); }
    catch (...) { config_.ai.max_history_rounds = 10; }

    config_.onebot.ws_url = get_window_text_u8(edit_ws_url_);
    config_.onebot.access_token = get_window_text_u8(edit_access_token_);
    try { config_.onebot.bot_qq = std::stoll(get_window_text_u8(edit_bot_qq_)); }
    catch (...) { config_.onebot.bot_qq = 0; }
    config_.onebot.group_require_at = (SendMessageW(chk_group_require_at_, BM_GETCHECK, 0, 0) == BST_CHECKED);

    auto split_into = [](const std::string& text, std::vector<std::string>& out) {
        out.clear();
        std::stringstream ss(text);
        std::string item;
        while (std::getline(ss, item, ',')) {
            auto b = item.find_first_not_of(" \t\r\n");
            auto e = item.find_last_not_of(" \t\r\n");
            if (b != std::string::npos) out.push_back(item.substr(b, e - b + 1));
        }
    };
    split_into(get_window_text_u8(edit_at_exempt_), config_.onebot.at_exempt_plugins);
    split_into(get_window_text_u8(edit_prefixes_), config_.onebot.trigger_prefixes);

    // 布局与主题一并落盘
    config_.ui.modules.clear();
    for (int i = 0; i < MOD_COUNT; ++i) {
        UILayoutItem it;
        it.x = mod_rc_[i].left;
        it.y = mod_rc_[i].top;
        it.w = mod_rc_[i].right - mod_rc_[i].left;
        it.h = mod_rc_[i].bottom - mod_rc_[i].top;
        it.visible = mod_visible_[i];
        config_.ui.modules.push_back(it);
    }
    config_.ui.theme = theme_index();
    config_.save_to_file("config.json");
}

// ---------------------------------------------------------------------------
// 状态 / 插件
// ---------------------------------------------------------------------------
void MainWindow::update_status_ui() {
    if (btn_toggle_bot_) {
        SetWindowTextW(btn_toggle_bot_, is_bot_running_ ? L"停止机器人" : L"启动机器人");
        InvalidateRect(btn_toggle_bot_, NULL, TRUE);
    }
    if (h_wnd_) InvalidateRect(h_wnd_, NULL, TRUE);
}

void MainWindow::refresh_plugin_ui() {
    auto list = PluginManager::get_instance().get_plugin_list();
    int enabled = 0;
    for (const auto& p : list) if (p.enabled) ++enabled;
    std::wstring txt = L"已加载 " + std::to_wstring(list.size()) + L" 个插件，" +
                       std::to_wstring(enabled) + L" 个启用中";
    if (label_plugin_summary_) SetWindowTextW(label_plugin_summary_, txt.c_str());
    if (h_wnd_) InvalidateRect(h_wnd_, NULL, TRUE);
}

// ---------------------------------------------------------------------------
// 行为
// ---------------------------------------------------------------------------
void MainWindow::toggle_bot_state() {
    if (is_bot_running_) {
        if (bot_client_) { bot_client_->stop(); bot_client_.reset(); }
        is_bot_running_ = false;
        update_status_ui();
        log("机器人已停止");
    } else {
        save_ui_to_config();
        if (config_.ai.api_key.empty()) {
            SetFocus(edit_api_key_);
            MessageBoxW(h_wnd_, L"请先在「AI 接口配置」模块填写 API Key。", L"缺少配置", MB_OK | MB_ICONWARNING);
            return;
        }
        log("正在连接 OneBot: " + config_.onebot.ws_url + " ...");
        bot_client_ = std::make_unique<BotClient>(config_);
        bot_client_->start();
        is_bot_running_ = true;
        update_status_ui();
        log("机器人已上线，正在监听消息");
    }
}

void MainWindow::test_api_connection() {
    save_ui_to_config();
    log("正在测试 AI 接口: " + config_.ai.base_url + " ...");

    std::thread([this]() {
        AIClient client(config_.ai);
        std::string reply = client.chat("test_ping", "请回复两个字：收到");
        if (reply.find("【AI") != std::string::npos) {
            log("接口测试失败: " + reply);
            MessageBoxW(h_wnd_, utf8_to_wstring("接口测试失败：\r\n" + reply).c_str(),
                        L"测试失败", MB_OK | MB_ICONERROR);
        } else {
            log("接口测试通过，模型回复: " + reply);
            MessageBoxW(h_wnd_, L"接口连通，大模型已就绪。", L"测试成功", MB_OK | MB_ICONINFORMATION);
        }
    }).detach();
}

void MainWindow::log(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        pending_logs_.push_back(message);
    }
    if (h_wnd_) PostMessageW(h_wnd_, WM_USER_LOG, 0, 0);
}

void MainWindow::append_log_text(const std::wstring& text, const std::string& raw_for_scan) {
    HWND log = edit_log_;
    if (!log) return;

    std::wstring shown = text;
    strip_emoji(shown);

    SendMessageW(log, WM_SETREDRAW, FALSE, 0);

    if (log_is_riched_) {
        GETTEXTLENGTHEX gtl = {};
        gtl.flags = GTL_DEFAULT;
        gtl.codepage = 1200;
        LONG len = (LONG)SendMessageW(log, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
        if (len > 120000) {
            SendMessageW(log, EM_SETSEL, 0, 50000);
            SendMessageW(log, EM_REPLACESEL, FALSE, (LPARAM)L"");
        }
        SendMessageW(log, EM_SETSEL, -1, -1);

        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
        cf.crTextColor = log_color_for(raw_for_scan);
        cf.yHeight = 200;
        lstrcpynW(cf.szFaceName, L"Microsoft YaHei UI", LF_FACESIZE);
        SendMessageW(log, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    } else {
        int len = GetWindowTextLengthW(log);
        if (len > 120000) {
            SendMessageW(log, EM_SETSEL, 0, 50000);
            SendMessageW(log, EM_REPLACESEL, FALSE, (LPARAM)L"");
            len = GetWindowTextLengthW(log);
        }
        SendMessageW(log, EM_SETSEL, len, len);
    }

    SendMessageW(log, EM_REPLACESEL, FALSE, (LPARAM)shown.c_str());
    SendMessageW(log, EM_SCROLLCARET, 0, 0);
    SendMessageW(log, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(log, NULL, TRUE);
}

void MainWindow::run_message_loop() {
    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// ---------------------------------------------------------------------------
// 窗口过程
// ---------------------------------------------------------------------------
LRESULT CALLBACK MainWindow::window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = (MainWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    const ThemePalette& t = theme();

    switch (uMsg) {
    case WM_USER_LOG: {
        if (!self || !self->edit_log_) return 0;
        std::vector<std::string> batch;
        {
            std::lock_guard<std::mutex> lock(self->log_mutex_);
            batch.swap(self->pending_logs_);
        }
        if (batch.empty()) return 0;

        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now;
        localtime_s(&tm_now, &tt);

        std::wstringstream prefix;
        prefix << L"[" << std::setw(2) << std::setfill(L'0') << tm_now.tm_hour << L":"
               << std::setw(2) << std::setfill(L'0') << tm_now.tm_min << L":"
               << std::setw(2) << std::setfill(L'0') << tm_now.tm_sec << L"]  ";

        SendMessageW(self->edit_log_, WM_SETREDRAW, FALSE, 0);
        for (const auto& m : batch) {
            self->append_log_text(prefix.str() + utf8_to_wstring(m) + L"\r\n", m);
            if (m.find("[BotClient] -> 回复成功") != std::string::npos) {
                self->total_msg_handled_++;
            }
        }
        SendMessageW(self->edit_log_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(self->edit_log_, NULL, TRUE);
        InvalidateRect(hwnd, NULL, TRUE);
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
            int w = LOWORD(lParam), h = HIWORD(lParam);
            if (w > 0 && h > 0) {
                for (int i = 0; i < MainWindow::MOD_COUNT; ++i) {
                    RECT& r = self->mod_rc_[i];
                    if (r.right > w - 8) { int d = r.right - (w - 8); r.left -= d; r.right -= d; }
                    if (r.bottom > h - 8) { int d = r.bottom - (h - 8); r.top -= d; r.bottom -= d; }
                    if (r.left < 8) { int d = 8 - r.left; r.left += d; r.right += d; }
                    if (r.top < Lay::Top) { int d = Lay::Top - r.top; r.top += d; r.bottom += d; }
                }
                self->relayout_all();
            }
        }
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 980;
        mmi->ptMinTrackSize.y = 660;
        return 0;
    }

    case WM_ERASEBKGND: {
        HDC hdcErase = (HDC)wParam;
        RECT rcErase;
        GetClientRect(hwnd, &rcErase);
        HBRUSH br = self ? self->brush_win_bg_ : (HBRUSH)GetStockObject(WHITE_BRUSH);
        FillRect(hdcErase, &rcErase, br);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (!self) { EndPaint(hwnd, &ps); return 0; }

        FillRect(hdc, &rc, self->brush_win_bg_);

        // 顶栏
        RECT rcHeader = { 0, 0, rc.right, Lay::HeaderH };
        FillRect(hdc, &rcHeader, self->brush_card_bg_);
        HPEN pen = CreatePen(PS_SOLID, 1, t.border_soft);
        HGDIOBJ op = SelectObject(hdc, pen);
        MoveToEx(hdc, 0, Lay::HeaderH, NULL);
        LineTo(hdc, rc.right, Lay::HeaderH);
        SelectObject(hdc, op);
        DeleteObject(pen);

        RECT rcLogo = { Lay::Pad, 13, Lay::Pad + 36, 49 };
        ui_fill_round(hdc, rcLogo, 10, t.primary);
        {
            HGDIOBJ old = SelectObject(hdc, self->font_brand_);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            RECT tr = rcLogo;
            DrawTextW(hdc, L"T", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, old);
        }
        {
            HGDIOBJ old = SelectObject(hdc, self->font_brand_);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, t.text_main);
            RECT tr = { Lay::Pad + 48, 12, Lay::Pad + 260, 38 };
            DrawTextW(hdc, L"TabMind", -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);
            SelectObject(hdc, old);

            SelectObject(hdc, self->font_small_);
            SetTextColor(hdc, t.text_sub);
            RECT tr2 = { Lay::Pad + 50, 36, Lay::Pad + 330, 54 };
            DrawTextW(hdc, L"AI Agent Console · OneBot v11", -1, &tr2,
                      DT_LEFT | DT_TOP | DT_SINGLELINE);
        }

        int active = self->drag_mod_;
        for (int idx : self->mod_order_) {
            if (idx == active) continue;      // 正在拖的模块最后画
            if (self->mod_visible_[idx]) self->paint_module(hdc, idx);
        }
        if (active >= 0 && active < MainWindow::MOD_COUNT && self->mod_visible_[active]) {
            self->paint_module(hdc, active);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (!self) break;
        const bool hover = self->hovered_.count((int)dis->CtlID) > 0;
        const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        wchar_t txt[64] = {};
        GetWindowTextW(dis->hwndItem, txt, 64);

        if (dis->CtlID == IDC_BTN_TOGGLE_BOT) {
            COLORREF bg = self->is_bot_running_ ? t.danger : t.primary;
            if (hover) bg = self->is_bot_running_ ? RGB(220, 38, 38) : t.primary_dark;
            if (pressed) bg = self->is_bot_running_ ? RGB(185, 28, 28) : t.primary_dark;
            ui_fill_round(dis->hDC, dis->rcItem, 10, bg);
            HGDIOBJ old = SelectObject(dis->hDC, self->font_h2_);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, RGB(255, 255, 255));
            DrawTextW(dis->hDC, txt, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dis->hDC, old);
            return TRUE;
        }

        if (dis->CtlID == IDC_BTN_THEME || dis->CtlID == IDC_BTN_LAYOUT_MENU ||
            dis->CtlID == IDC_BTN_CLEAR_LOG) {
            COLORREF bg = pressed ? t.border : (hover ? t.field_bg : t.card_bg);
            ui_fill_round(dis->hDC, dis->rcItem, 8, bg, t.border);
            HGDIOBJ old = SelectObject(dis->hDC, self->font_small_);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, dis->CtlID == IDC_BTN_LAYOUT_MENU ? t.text_main : t.primary);
            DrawTextW(dis->hDC, txt, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dis->hDC, old);
            return TRUE;
        }

        if (dis->CtlID == IDC_BTN_SAVE_CONFIG || dis->CtlID == IDC_BTN_TEST_API ||
            dis->CtlID == IDC_BTN_RELOAD_PLUGINS || dis->CtlID == IDC_BTN_OPEN_PLUGIN_DIR ||
            dis->CtlID == IDC_BTN_PLUGIN_MGR || dis->CtlID == IDC_BTN_ADVANCED ||
            dis->CtlID == IDC_BTN_HELP || dis->CtlID == IDC_BTN_RESET_LAYOUT) {
            COLORREF bg = pressed ? t.border : (hover ? t.field_bg : t.card_bg);
            COLORREF bd = hover ? t.text_faint : t.border;
            ui_fill_round(dis->hDC, dis->rcItem, 9, bg, bd);
            HGDIOBJ old = SelectObject(dis->hDC, self->font_body_bold_);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, t.text_main);
            DrawTextW(dis->hDC, txt, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dis->hDC, old);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, t.card_bg);
        SetBkMode(hdc, OPAQUE);
        SetTextColor(hdc, t.text_sub);
        return (LRESULT)(self ? self->brush_card_bg_ : (HBRUSH)GetStockObject(WHITE_BRUSH));
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        HWND ctl = (HWND)lParam;
        if (self && ctl == self->edit_log_) {
            SetBkColor(hdc, t.log_bg);
            SetTextColor(hdc, t.log_text);
            return (LRESULT)(self ? self->brush_log_bg_ : (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
        SetBkColor(hdc, t.field_bg);
        SetTextColor(hdc, t.text_main);
        return (LRESULT)(self ? self->brush_field_bg_ : (HBRUSH)GetStockObject(WHITE_BRUSH));
    }

    case WM_LBUTTONDOWN: {
        if (!self || self->config_.ui.locked) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        bool grip = false;
        int id = self->module_at(pt, grip);
        if (id < 0) break;
        // 点击即置顶：卡片绘制顺序 + 子控件窗口顺序都要提到最上
        auto& order = self->mod_order_;
        auto it = std::find(order.begin(), order.end(), id);
        if (it != order.end()) {
            order.erase(it);
            order.push_back(id);
        }
        for (HWND h : self->mod_ctl_[id]) {
            if (h) SetWindowPos(h, HWND_TOP, 0, 0, 0, 0,
                                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        self->drag_mode_ = grip ? 2 : 1;
        self->drag_mod_ = id;
        self->drag_start_ = pt;
        self->drag_rc0_ = self->mod_rc_[id];
        SetCapture(hwnd);
        SetCursor(LoadCursor(NULL, grip ? IDC_SIZENWSE : IDC_SIZEALL));
        return 0;
    }

    case WM_SETCURSOR: {
        if (!self) break;
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            bool grip = false;
            int id = self->module_at(pt, grip);
            if (id >= 0 && !self->config_.ui.locked) {
                SetCursor(LoadCursor(NULL, grip ? IDC_SIZENWSE : IDC_SIZEALL));
                return TRUE;
            }
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        if (!self || self->drag_mode_ == 0 || self->drag_mod_ < 0) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int dx = pt.x - self->drag_start_.x;
        int dy = pt.y - self->drag_start_.y;
        RECT rc = self->drag_rc0_;
        if (self->drag_mode_ == 1) {
            rc.left += dx; rc.right += dx;
            rc.top += dy;  rc.bottom += dy;
        } else {
            rc.right += dx; rc.bottom += dy;
        }

        auto snap = [](int v) { return (v >= 0) ? ((v + 4) / 8 * 8) : -(((-v) + 4) / 8 * 8); };
        rc.left = snap(rc.left); rc.top = snap(rc.top);
        rc.right = snap(rc.right); rc.bottom = snap(rc.bottom);

        int minw = (self->drag_mod_ == MainWindow::MOD_STATUS) ? 260 : 300;
        int minh = (self->drag_mod_ == MainWindow::MOD_LOG) ? 180 : 200;
        if (rc.right - rc.left < minw) rc.right = rc.left + minw;
        if (rc.bottom - rc.top < minh) rc.bottom = rc.top + minh;

        RECT cr;
        GetClientRect(hwnd, &cr);
        if (rc.left < 8) { int d = 8 - rc.left; rc.left += d; rc.right += d; }
        if (rc.top < Lay::Top) { int d = Lay::Top - rc.top; rc.top += d; rc.bottom += d; }
        if (rc.right > cr.right - 8) { int d = rc.right - (cr.right - 8); rc.left -= d; rc.right -= d; }
        if (rc.bottom > cr.bottom - 8) { int d = rc.bottom - (cr.bottom - 8); rc.top -= d; rc.bottom -= d; }

        // 与其他模块重叠就不采用这个位置：卡片背景由父窗口绘制，
        // 一旦重叠，被压住卡片的子控件会透出来，视觉上会「穿模」。
        for (int i = 0; i < MainWindow::MOD_COUNT; ++i) {
            if (i == self->drag_mod_ || !self->mod_visible_[i]) continue;
            const RECT& o = self->mod_rc_[i];
            if (rc.left < o.right && o.left < rc.right && rc.top < o.bottom && o.top < rc.bottom) {
                return 0;
            }
        }

        self->mod_rc_[self->drag_mod_] = rc;
        self->relayout_module(self->drag_mod_);
        InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);
        return 0;
    }

    case WM_LBUTTONUP: {
        if (!self || self->drag_mode_ == 0) break;
        ReleaseCapture();
        int id = self->drag_mod_;
        self->drag_mode_ = 0;
        self->drag_mod_ = -1;

        // 放置时自动避让：与其它模块重叠就向右/向下推开，实在放不下则退回原位。
        // （卡片背景由父窗口绘制，若互相重叠，被压住卡片的子控件会透出来，所以禁止重叠）
        if (id >= 0) {
            auto overlaps = [&](const RECT& a, const RECT& b) {
                return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
            };
            auto collides = [&](const RECT& r) {
                for (int i = 0; i < MainWindow::MOD_COUNT; ++i) {
                    if (i == id || !self->mod_visible_[i]) continue;
                    if (overlaps(r, self->mod_rc_[i])) return true;
                }
                return false;
            };
            RECT r = self->mod_rc_[id];
            if (collides(r)) {
                RECT cr;
                GetClientRect(hwnd, &cr);
                bool placed = false;
                RECT found = r;
                for (int step = 1; step <= 60 && !placed; ++step) {
                    int off = step * 16;
                    RECT t = r;
                    t.left += off; t.right += off;
                    if (t.right <= cr.right - 8 && t.left >= 8 && !collides(t)) { found = t; placed = true; break; }
                    t = r;
                    t.top += off; t.bottom += off;
                    if (t.bottom <= cr.bottom - 8 && t.top >= Lay::Top && !collides(t)) { found = t; placed = true; break; }
                }
                self->mod_rc_[id] = placed ? found : self->drag_rc0_;
                self->relayout_module(id);
            }
        }

        self->save_layout();
        RedrawWindow(hwnd, NULL, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        return 0;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        if (!self) break;
        switch (wmId) {
        case IDC_BTN_TOGGLE_BOT: self->toggle_bot_state(); break;
        case IDC_BTN_SAVE_CONFIG:
            self->save_ui_to_config();
            self->log("配置已保存到 config.json");
            MessageBoxW(hwnd, L"配置已保存。", L"保存成功", MB_OK | MB_ICONINFORMATION);
            break;
        case IDC_BTN_TEST_API: self->test_api_connection(); break;
        case IDC_BTN_CLEAR_LOG:
            SetWindowTextW(self->edit_log_, L"");
            self->log("日志已清空");
            break;
        case IDC_BTN_RELOAD_PLUGINS:
            PluginManager::get_instance().load_plugins("plugins");
            self->refresh_plugin_ui();
            self->log("插件已重新加载");
            break;
        case IDC_BTN_OPEN_PLUGIN_DIR:
            CreateDirectoryW(L"plugins", NULL);
            ShellExecuteW(NULL, L"open", L"plugins", NULL, NULL, SW_SHOW);
            break;
        case IDC_BTN_ADVANCED:
            if (ui_dialogs::show_advanced_settings(hwnd, self->config_, self->h_instance_)) {
                self->config_.save_to_file("config.json");
                self->update_status_ui();
                self->log("高级设置已保存");
            }
            break;
        case IDC_BTN_PLUGIN_MGR:
            ui_dialogs::show_plugin_manager(hwnd, self->h_instance_);
            self->refresh_plugin_ui();
            break;
        case IDC_BTN_HELP:
            ui_dialogs::show_help(hwnd, self->h_instance_);
            break;
        case IDC_BTN_THEME:
            self->apply_theme((theme_index() + 1) % kThemeCount, true);
            self->log(std::string("主题已切换: ") + wstring_to_utf8(std::wstring(theme().name)));
            break;
        case IDC_BTN_LAYOUT_MENU:
            self->show_layout_menu();
            break;
        case IDC_BTN_RESET_LAYOUT: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            self->default_layout(rc.right, rc.bottom);
            self->relayout_all();
            self->save_layout();
            self->log("布局已重置为默认");
            break;
        }
        }
        return 0;
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
