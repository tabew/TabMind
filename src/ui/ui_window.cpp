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
#define IDC_BTN_THEME            1011

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

namespace Lay {
    const int Pad = 16;
    const int RowH = 30;
    const int GapY = 20;
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

static void strip_emoji(std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() &&
            s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) { ++i; continue; }
        if ((c >= 0x2600 && c <= 0x27BF) || (c >= 0x2B00 && c <= 0x2BFF) ||
            c == 0xFE0F || c == 0xFE0E || c == 0x200D ||
            c == 0x2139 || c == 0x2049 || c == 0x203C) continue;
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

struct MenuEntry { const wchar_t* title; const wchar_t* desc; };
static const MenuEntry kMenu[6] = {
    { L"运行状态",        L"在线状态 · 当前模型 · 回复数" },
    { L"AI 接口配置",     L"接口地址 · 密钥 · 模型 · 提示词" },
    { L"OneBot / QQ 接入", L"WS 地址 · Bot QQ · 前缀 · 免@插件" },
    { L"扩展插件",        L"插件清单与启用开关" },
    { L"快捷操作",        L"启动停止 · 保存 · 测试 · 高级设置" },
    { L"运行日志",        L"实时交互日志（按级别着色）" },
};

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

const wchar_t* MainWindow::module_title(int kind) {
    switch (kind) {
    case MOD_STATUS: return L"运行状态";
    case MOD_AI:     return L"AI 接口配置";
    case MOD_QQ:     return L"OneBot / QQ 接入";
    case MOD_PLUGIN: return L"扩展插件";
    case MOD_ACTION: return L"快捷操作";
    case MOD_LOG:    return L"运行日志";
    default:         return L"";
    }
}

void MainWindow::apply_theme(int index, bool persist) {
    set_theme(index);
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

    if (btn_theme_) SetWindowTextW(btn_theme_, (std::wstring(L"主题 · ") + theme().name).c_str());

    for (int k = 0; k < MOD_COUNT; ++k) {
        for (HWND h : mod_ctl_[k]) {
            if (!h) continue;
            SetWindowTheme(h, t.dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
            InvalidateRect(h, NULL, TRUE);
        }
        if (mod_wnd_[k]) RedrawWindow(mod_wnd_[k], NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }

    if (persist) {
        config_.ui.theme = theme_index();
        config_.save_to_file("config.json");
    }
    if (h_wnd_) RedrawWindow(h_wnd_, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

LRESULT CALLBACK MainWindow::button_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                 UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    MainWindow* self = (MainWindow*)dwRefData;
    switch (uMsg) {
    case WM_ERASEBKGND: {
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
// 启动
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

    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWindow::launcher_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TabMind_Launcher";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = brush_win_bg_;
    RegisterClassW(&wc);

    WNDCLASSW wm = {};
    wm.lpfnWndProc = MainWindow::module_proc;
    wm.hInstance = hInstance;
    wm.lpszClassName = L"TabMind_Module";
    wm.hCursor = LoadCursor(NULL, IDC_ARROW);
    wm.hbrBackground = brush_win_bg_;
    RegisterClassW(&wm);

    const int win_w = 372, win_h = 574;
    RECT wa = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.right - win_w - 30;
    int y = wa.top + 40;
    if (x < wa.left + 10) x = wa.left + 40;

    h_wnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, L"TabMind_Launcher", L"TabMind · 菜单",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             x, y, win_w, win_h, NULL, NULL, hInstance, this);
    if (!h_wnd_) return false;

    create_module_windows();
    load_config_to_ui();
    update_status_ui();
    PluginManager::get_instance().load_plugins("plugins");
    refresh_plugin_ui();

    ShowWindow(h_wnd_, nCmdShow);
    SetForegroundWindow(h_wnd_);
    UpdateWindow(h_wnd_);

    log("TabMind 已就绪 · 从菜单打开各个功能窗口");
    log("OneBot 服务端: " + config_.onebot.ws_url);

    if (!config_.ai.api_key.empty()) {
        log("正在连接 OneBot WebSocket ...");
        bot_client_ = std::make_unique<BotClient>(config_);
        bot_client_->start();
        is_bot_running_ = true;
        update_status_ui();
        log("机器人已上线，正在监听消息");
    } else {
        log("未配置 API Key，请打开「AI 接口配置」窗口填写");
    }
    return true;
}

void MainWindow::init_launcher(HWND hwnd) {
    btn_theme_ = CreateWindowW(L"BUTTON", (std::wstring(L"主题 · ") + theme().name).c_str(),
                               WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                               16, 486, 324, 34, hwnd, (HMENU)(INT_PTR)IDC_BTN_THEME, h_instance_, NULL);
    (void)0;
    SetWindowSubclass(btn_theme_, MainWindow::button_subclass_proc, IDC_BTN_THEME, (DWORD_PTR)this);
}

// ---------------------------------------------------------------------------
// 菜单绘制
// ---------------------------------------------------------------------------
void MainWindow::paint_launcher(HDC hdc, const RECT& rc) {
    const ThemePalette& t = theme();
    FillRect(hdc, &rc, brush_win_bg_);

    RECT rcLogo = { 20, 18, 56, 54 };
    ui_fill_round(hdc, rcLogo, 10, t.primary);
    {
        HGDIOBJ old = SelectObject(hdc, font_brand_);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        RECT tr = rcLogo;
        DrawTextW(hdc, L"T", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, old);
    }
    {
        HGDIOBJ old = SelectObject(hdc, font_brand_);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, t.text_main);
        RECT tr = { 66, 16, rc.right - 16, 42 };
        DrawTextW(hdc, L"TabMind", -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(hdc, old);

        SelectObject(hdc, font_small_);
        SetTextColor(hdc, t.text_sub);
        RECT tr2 = { 68, 40, rc.right - 16, 58 };
        DrawTextW(hdc, L"AI Agent Console · OneBot v11", -1, &tr2, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }

    // 状态条
    {
        bool run = is_bot_running_;
        RECT rcPill = { 20, 66, rc.right - 20, 94 };
        ui_fill_round(hdc, rcPill, 14, run ? t.success_soft : t.danger_soft);

        HBRUSH dot = CreateSolidBrush(run ? t.success : t.danger);
        HGDIOBJ ob = SelectObject(hdc, dot);
        HGDIOBJ opn = SelectObject(hdc, GetStockObject(NULL_PEN));
        Ellipse(hdc, 32, 76, 42, 86);
        SelectObject(hdc, ob);
        SelectObject(hdc, opn);
        DeleteObject(dot);

        HGDIOBJ old = SelectObject(hdc, font_small_);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, run ? t.success_text : t.danger);
        RECT tr = { 50, 66, rc.right - 24, 94 };
        std::wstring s = run ? L"运行中" : L"已停止";
        s += L" · " + utf8_to_wstring(config_.ai.model) + L" · 已回复 " + std::to_wstring(total_msg_handled_);
        DrawTextW(hdc, s.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, old);
    }

    // 菜单项：按客户区高度自适应，保证 6 行 + 底部按钮都放得下
    const int top = 100;
    int rowH = (rc.bottom - top - 56) / 6;
    if (rowH > 62) rowH = 62;
    if (rowH < 42) rowH = 42;
    for (int i = 0; i < 6; ++i) {
        RECT row = { 16, top + i * rowH, rc.right - 16, top + i * rowH + rowH - 8 };
        bool open = mod_open_[i];
        bool hover = (hover_row_ == i);

        COLORREF bg = hover ? (t.dark ? t.field_bg : t.primary_soft) : t.card_bg;
        COLORREF bd = hover ? t.primary : t.border;
        ui_fill_round(hdc, row, 10, bg, bd);

        RECT bar = { row.left, row.top + 12, row.left + 4, row.bottom - 12 };
        ui_fill_round(hdc, bar, 2, open ? t.primary : t.border);

        HGDIOBJ old = SelectObject(hdc, font_body_bold_);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, t.text_main);
        RECT tr = { row.left + 18, row.top + 8, row.right - 80, row.top + 30 };
        DrawTextW(hdc, kMenu[i].title, -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);

        SelectObject(hdc, font_small_);
        SetTextColor(hdc, t.text_sub);
        RECT tr2 = { row.left + 18, row.top + 30, row.right - 16, row.top + 48 };
        DrawTextW(hdc, kMenu[i].desc, -1, &tr2, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

        SetTextColor(hdc, open ? t.primary : t.text_faint);
        RECT tr3 = { row.right - 76, row.top + 8, row.right - 14, row.top + 30 };
        DrawTextW(hdc, open ? L"● 已打开" : L"○ 打开", -1, &tr3, DT_RIGHT | DT_TOP | DT_SINGLELINE);
        SelectObject(hdc, old);
    }
}

int MainWindow::launcher_row_at(POINT pt) const {
    RECT cr = {};
    if (h_wnd_) GetClientRect(h_wnd_, &cr);
    if (cr.right <= 0) cr.right = 372;
    const int top = 100;
    int rowH = (cr.bottom - top - 56) / 6;
    if (rowH > 62) rowH = 62;
    if (rowH < 42) rowH = 42;
    for (int i = 0; i < 6; ++i) {
        RECT row = { 16, top + i * rowH, cr.right - 16, top + i * rowH + rowH - 8 };
        if (pt.x >= row.left && pt.x < row.right && pt.y >= row.top && pt.y < row.bottom) return i;
    }
    return -1;
}

void MainWindow::toggle_module(int kind) {
    show_module(kind, !mod_open_[kind]);
}

// ---------------------------------------------------------------------------
// 模块窗口创建 / 显示
// ---------------------------------------------------------------------------
RECT MainWindow::default_module_rect(int kind) const {
    RECT wa = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int bw = 460, bh = 560;
    switch (kind) {
    case MOD_STATUS: bw = 420; bh = 190; break;
    case MOD_AI:     bw = 470; bh = 600; break;
    case MOD_QQ:     bw = 470; bh = 570; break;
    case MOD_PLUGIN: bw = 470; bh = 430; break;
    case MOD_ACTION: bw = 400; bh = 340; break;
    case MOD_LOG:    bw = 660; bh = 500; break;
    default: break;
    }
    // 每个窗口错开一点，避免全部叠在一起
    int x = wa.left + 40 + kind * 44;
    int y = wa.top + 40 + kind * 38;
    if (x + bw > wa.right - 8) x = wa.right - bw - 8;
    if (y + bh > wa.bottom - 8) y = wa.bottom - bh - 8;
    return RECT{ x, y, x + bw, y + bh };
}

void MainWindow::create_module_windows() {
    for (int k = 0; k < MOD_COUNT; ++k) create_module(k);
    // 全部创建完再统一显示，避免中途落盘把已保存布局冲掉
    for (int k = 0; k < MOD_COUNT; ++k) {
        if (mod_wnd_[k]) ShowWindow(mod_wnd_[k], mod_open_[k] ? SW_SHOW : SW_HIDE);
    }
}

void MainWindow::create_module(int kind) {
    if (mod_wnd_[kind]) return;

    bool has_cfg = (config_.ui.modules.size() == (size_t)MOD_COUNT);
    RECT rc = default_module_rect(kind);
    if (has_cfg) {
        const UILayoutItem& it = config_.ui.modules[kind];
        if (it.w > 200 && it.h > 120) rc = RECT{ it.x, it.y, it.x + it.w, it.y + it.h };
        mod_open_[kind] = it.visible;
    } else {
        // 首次运行：只显示菜单，所有功能窗口由用户点开
        mod_open_[kind] = false;
    }

    ModuleCtx* ctx = new ModuleCtx{ this, kind };
    std::wstring title = std::wstring(L"TabMind · ") + module_title(kind);
    mod_wnd_[kind] = CreateWindowExW(0, L"TabMind_Module", title.c_str(),
                                     WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                     rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                                     h_wnd_, NULL, h_instance_, ctx);
    if (!mod_wnd_[kind]) { delete ctx; return; }

    HWND p = mod_wnd_[kind];
    auto mk_label = [&](const wchar_t* text, HFONT font) {
        HWND h = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                               -3000, -3000, 10, 10, p, NULL, h_instance_, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
        return h;
    };
    auto mk_edit = [&](int id, DWORD style) {
        HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | style,
                                 -3000, -3000, 10, 10, p, (HMENU)(INT_PTR)id, h_instance_, NULL);
        SendMessageW(e, WM_SETFONT, (WPARAM)font_body_, TRUE);
        SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
        return e;
    };
    auto mk_button = [&](int id, const wchar_t* text) {
        HWND b = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                               -3000, -3000, 10, 10, p, (HMENU)(INT_PTR)id, h_instance_, NULL);
        SetWindowSubclass(b, MainWindow::button_subclass_proc, id, (DWORD_PTR)this);
        return b;
    };

    if (kind == MOD_AI) {
        mod_ctl_[kind].push_back(mk_label(L"API 接口地址 (Base URL)", font_small_));
        edit_base_url_ = mk_edit(IDC_EDIT_BASE_URL, 0);
        mod_ctl_[kind].push_back(edit_base_url_);
        mod_ctl_[kind].push_back(mk_label(L"API 密钥 (API Key)", font_small_));
        edit_api_key_ = mk_edit(IDC_EDIT_API_KEY, ES_PASSWORD);
        mod_ctl_[kind].push_back(edit_api_key_);
        mod_ctl_[kind].push_back(mk_label(L"模型", font_small_));
        mod_ctl_[kind].push_back(mk_label(L"Max Tokens", font_small_));
        mod_ctl_[kind].push_back(mk_label(L"上下文轮数", font_small_));

        combo_model_ = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                     -3000, -3000, 10, 200, p, (HMENU)IDC_COMBO_MODEL, h_instance_, NULL);
        SendMessageW(combo_model_, WM_SETFONT, (WPARAM)font_body_, TRUE);
        {
            const wchar_t* models[] = { L"deepseek-chat", L"deepseek-reasoner", L"deepseek-v4-flash",
                                        L"gpt-4o-mini", L"gpt-4o" };
            for (const wchar_t* m : models) SendMessageW(combo_model_, CB_ADDSTRING, 0, (LPARAM)m);
        }
        mod_ctl_[kind].push_back(combo_model_);
        edit_max_tokens_ = mk_edit(IDC_EDIT_MAX_TOKENS, ES_NUMBER);
        mod_ctl_[kind].push_back(edit_max_tokens_);
        edit_history_rounds_ = mk_edit(IDC_EDIT_HISTORY_ROUNDS, ES_NUMBER);
        mod_ctl_[kind].push_back(edit_history_rounds_);

        mod_ctl_[kind].push_back(mk_label(L"系统提示词 / 人设", font_small_));
        edit_system_prompt_ = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
            -3000, -3000, 10, 10, p, (HMENU)IDC_EDIT_SYSTEM_PROMPT, h_instance_, NULL);
        SendMessageW(edit_system_prompt_, WM_SETFONT, (WPARAM)font_body_, TRUE);
        SendMessageW(edit_system_prompt_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
        mod_ctl_[kind].push_back(edit_system_prompt_);
    } else if (kind == MOD_QQ) {
        mod_ctl_[kind].push_back(mk_label(L"OneBot WebSocket 地址", font_small_));
        edit_ws_url_ = mk_edit(IDC_EDIT_WS_URL, 0);
        mod_ctl_[kind].push_back(edit_ws_url_);
        mod_ctl_[kind].push_back(mk_label(L"机器人 QQ 号", font_small_));
        mod_ctl_[kind].push_back(mk_label(L"Access Token（无则留空）", font_small_));
        edit_bot_qq_ = mk_edit(IDC_EDIT_BOT_QQ, ES_NUMBER);
        mod_ctl_[kind].push_back(edit_bot_qq_);
        edit_access_token_ = mk_edit(IDC_EDIT_ACCESS_TOKEN, 0);
        mod_ctl_[kind].push_back(edit_access_token_);
        mod_ctl_[kind].push_back(mk_label(L"触发前缀词（逗号分隔）", font_small_));
        edit_prefixes_ = mk_edit(IDC_EDIT_PREFIXES, 0);
        mod_ctl_[kind].push_back(edit_prefixes_);

        chk_group_require_at_ = CreateWindowW(L"BUTTON", L"群聊必须 @机器人 或命中前缀才响应",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, -3000, -3000, 10, 10, p,
            (HMENU)IDC_CHK_REQUIRE_AT, h_instance_, NULL);
        SendMessageW(chk_group_require_at_, WM_SETFONT, (WPARAM)font_body_, TRUE);
        mod_ctl_[kind].push_back(chk_group_require_at_);

        mod_ctl_[kind].push_back(mk_label(L"免 @ 插件（逗号分隔）", font_small_));
        edit_at_exempt_ = mk_edit(IDC_EDIT_AT_EXEMPT, 0);
        mod_ctl_[kind].push_back(edit_at_exempt_);
    } else if (kind == MOD_PLUGIN) {
        label_plugin_summary_ = mk_label(L"正在加载插件...", font_body_);
        mod_ctl_[kind].push_back(label_plugin_summary_);
        btn_reload_plugins_ = mk_button(IDC_BTN_RELOAD_PLUGINS, L"重载插件");
        mod_ctl_[kind].push_back(btn_reload_plugins_);
        btn_open_plugin_dir_ = mk_button(IDC_BTN_OPEN_PLUGIN_DIR, L"插件目录");
        mod_ctl_[kind].push_back(btn_open_plugin_dir_);
        btn_plugin_mgr_ = mk_button(IDC_BTN_PLUGIN_MGR, L"插件管理");
        mod_ctl_[kind].push_back(btn_plugin_mgr_);
    } else if (kind == MOD_ACTION) {
        btn_toggle_bot_ = mk_button(IDC_BTN_TOGGLE_BOT, L"启动机器人");
        mod_ctl_[kind].push_back(btn_toggle_bot_);
        btn_save_config_ = mk_button(IDC_BTN_SAVE_CONFIG, L"保存配置");
        mod_ctl_[kind].push_back(btn_save_config_);
        btn_test_api_ = mk_button(IDC_BTN_TEST_API, L"测试接口");
        mod_ctl_[kind].push_back(btn_test_api_);
        btn_advanced_ = mk_button(IDC_BTN_ADVANCED, L"高级设置");
        mod_ctl_[kind].push_back(btn_advanced_);
        btn_help_ = mk_button(IDC_BTN_HELP, L"使用说明");
        mod_ctl_[kind].push_back(btn_help_);
    } else if (kind == MOD_LOG) {
        LoadLibraryW(L"Msftedit.dll");
        log_is_riched_ = (GetModuleHandleW(L"Msftedit.dll") != nullptr);
        const wchar_t* cls = log_is_riched_ ? L"RICHEDIT50W" : L"EDIT";
        edit_log_ = CreateWindowExW(0, cls, L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | ES_NOHIDESEL,
            -3000, -3000, 10, 10, p, (HMENU)IDC_EDIT_LOG, h_instance_, NULL);
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
        mod_ctl_[kind].push_back(edit_log_);
        btn_clear_log_ = mk_button(IDC_BTN_CLEAR_LOG, L"清空");
        mod_ctl_[kind].push_back(btn_clear_log_);
    }

    relayout_module(kind);
}

void MainWindow::show_module(int kind, bool open) {
    if (kind < 0 || kind >= MOD_COUNT || !mod_wnd_[kind]) return;
    mod_open_[kind] = open;
    ShowWindow(mod_wnd_[kind], open ? SW_SHOW : SW_HIDE);
    if (open) {
        SetWindowPos(mod_wnd_[kind], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetForegroundWindow(mod_wnd_[kind]);
    }

    config_.ui.modules.clear();
    for (int i = 0; i < MOD_COUNT; ++i) {
        RECT r{};
        if (mod_wnd_[i]) GetWindowRect(mod_wnd_[i], &r);
        UILayoutItem it;
        it.x = r.left; it.y = r.top;
        it.w = r.right - r.left; it.h = r.bottom - r.top;
        it.visible = mod_open_[i];
        config_.ui.modules.push_back(it);
    }
    config_.ui.theme = theme_index();
    config_.save_to_file("config.json");
    if (h_wnd_) InvalidateRect(h_wnd_, NULL, TRUE);
}

void MainWindow::relayout_module(int kind) {
    HWND p = mod_wnd_[kind];
    if (!p) return;

    RECT cr;
    GetClientRect(p, &cr);
    int W = cr.right, H = cr.bottom;
    const int P = Lay::Pad;
    const int rowH = Lay::RowH;
    const int gapY = Lay::GapY;

    auto place = [](HWND h, int x, int y, int w, int hh) {
        if (h) SetWindowPos(h, NULL, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE);
    };

    if (kind == MOD_AI) {
        auto& c = mod_ctl_[kind];
        int L = P, CW = W - P * 2;
        int y = P;
        place(c[0], L, y, CW, 16);
        place(edit_base_url_, L, y + 17, CW, rowH);
        y += 17 + rowH + gapY;
        place(c[2], L, y, CW, 16);
        place(edit_api_key_, L, y + 17, CW, rowH);
        y += 17 + rowH + gapY;

        int cModel = CW * 50 / 100;
        int cTok = CW * 21 / 100;
        int cRound = CW - cModel - cTok - 16;
        if (cRound < 46) cRound = 46;
        place(c[4], L, y, cModel, 16);
        place(combo_model_, L, y + 17, cModel, 220);
        place(c[5], L + cModel + 8, y, cTok, 16);
        place(edit_max_tokens_, L + cModel + 8, y + 17, cTok, rowH);
        place(c[6], L + cModel + cTok + 16, y, cRound, 16);
        place(edit_history_rounds_, L + cModel + cTok + 16, y + 17, cRound, rowH);
        y += 17 + rowH + gapY;

        place(c[10], L, y, CW, 16);
        int ph = H - 12 - (y + 17);
        if (ph < 60) ph = 60;
        place(edit_system_prompt_, L, y + 17, CW, ph);
    } else if (kind == MOD_QQ) {
        auto& c = mod_ctl_[kind];
        int L = P, CW = W - P * 2;
        int y = P;
        place(c[0], L, y, CW, 16);
        place(edit_ws_url_, L, y + 17, CW, rowH);
        y += 17 + rowH + gapY;

        int cBot = CW * 40 / 100;
        place(c[2], L, y, cBot, 16);
        place(c[3], L + cBot + 8, y, CW - cBot - 8, 16);
        place(edit_bot_qq_, L, y + 17, cBot, rowH);
        place(edit_access_token_, L + cBot + 8, y + 17, CW - cBot - 8, rowH);
        y += 17 + rowH + gapY;

        place(c[6], L, y, CW, 16);
        place(edit_prefixes_, L, y + 17, CW, rowH);
        y += 17 + rowH + gapY;

        place(chk_group_require_at_, L, y, CW, 22);
        y += 30;
        place(c[9], L, y, CW, 16);
        place(edit_at_exempt_, L, y + 17, CW, rowH);
    } else if (kind == MOD_PLUGIN) {
        auto& c = mod_ctl_[kind];
        int L = P, CW = W - P * 2;
        place(c[0], L, P, CW, 34);
        int y = P + 42;
        int bw = (CW - 16) / 3;
        if (bw < 68) bw = 68;
        place(btn_reload_plugins_, L, y, bw, 32);
        place(btn_open_plugin_dir_, L + bw + 8, y, bw, 32);
        place(btn_plugin_mgr_, L + (bw + 8) * 2, y, bw, 32);
    } else if (kind == MOD_ACTION) {
        int L = P, CW = W - P * 2;
        int y = P;
        place(btn_toggle_bot_, L, y, CW, 46);
        y += 58;
        int half = (CW - 10) / 2;
        place(btn_save_config_, L, y, half, 36);
        place(btn_test_api_, L + half + 10, y, half, 36);
        y += 46;
        place(btn_advanced_, L, y, half, 36);
        place(btn_help_, L + half + 10, y, half, 36);
    } else if (kind == MOD_LOG) {
        place(btn_clear_log_, W - P - 56, 10, 56, 26);
        int y = 44;
        int hh = H - y - P;
        if (hh < 60) hh = 60;
        place(edit_log_, P - 6, y, W - (P - 6) * 2, hh);
        if (log_is_riched_) {
            RECT fr = { 12, 8, W - (P - 6) * 2 - 12, hh - 8 };
            SendMessageW(edit_log_, EM_SETRECT, 0, (LPARAM)&fr);
        }
    }
}

void MainWindow::paint_module_window(int kind, HDC hdc, const RECT& rc) {
    const ThemePalette& t = theme();
    FillRect(hdc, &rc, brush_card_bg_);

    if (kind == MOD_STATUS) {
        int L = Lay::Pad, W = rc.right - Lay::Pad * 2;
        int y = 18;
        int w1 = W * 30 / 100, w2 = W * 42 / 100, w3 = W - w1 - w2 - 16;
        if (w3 < 60) w3 = 60;
        RECT r1 = { L, y, L + w1, y + 32 };
        RECT r2 = { L + w1 + 8, y, L + w1 + 8 + w2, y + 32 };
        RECT r3 = { L + w1 + w2 + 16, y, L + w1 + w2 + 16 + w3, y + 32 };
        bool run = is_bot_running_;
        ui_draw_pill(hdc, r1, run ? t.success_soft : t.danger_soft,
                     run ? t.success_text : t.danger,
                     run ? L"● 运行中" : L"● 已停止", font_small_);
        ui_draw_pill(hdc, r2, t.primary_soft, t.primary,
                     L"模型 " + utf8_to_wstring(config_.ai.model), font_small_);
        ui_draw_pill(hdc, r3, t.field_bg, t.text_main,
                     L"已回复 " + std::to_wstring(total_msg_handled_), font_small_);

        HGDIOBJ old = SelectObject(hdc, font_small_);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, t.text_faint);
        RECT rcHint = { L, y + 44, rc.right - Lay::Pad, y + 66 };
        DrawTextW(hdc, L"窗口可自由拖动缩放；关闭后可在菜单里重新打开",
                  -1, &rcHint, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, old);
    } else if (kind == MOD_PLUGIN) {
        int L = Lay::Pad, W = rc.right - Lay::Pad * 2;
        int list_top = Lay::Pad + 92;
        int row_h = 28;
        int max_rows = (rc.bottom - Lay::Pad - 18 - list_top) / row_h;
        if (max_rows < 1) max_rows = 1;
        auto plugins = PluginManager::get_instance().get_plugin_list();

        HGDIOBJ old = SelectObject(hdc, font_small_);
        SetBkMode(hdc, TRANSPARENT);
        if (plugins.empty()) {
            SetTextColor(hdc, t.text_faint);
            RECT rcE = { L, list_top, L + W, list_top + 20 };
            DrawTextW(hdc, L"暂无插件 · 把 DLL 放进 plugins 目录", -1, &rcE,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            int shown = 0;
            for (const auto& pl : plugins) {
                if (shown >= max_rows) break;
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
                std::wstring more = L"还有 " + std::to_wstring(plugins.size() - shown) + L" 个";
                DrawTextW(hdc, more.c_str(), -1, &rcMore, DT_LEFT | DT_TOP | DT_SINGLELINE);
            }
        }
        SelectObject(hdc, old);
    } else if (kind == MOD_ACTION) {
        RECT rcHint = { Lay::Pad, rc.bottom - 26, rc.right - Lay::Pad, rc.bottom - 8 };
        HGDIOBJ old = SelectObject(hdc, font_small_);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, t.text_faint);
        DrawTextW(hdc, L"启动前请先在「AI 接口配置」填好密钥", -1, &rcHint,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, old);
    }
}

// ---------------------------------------------------------------------------
// 配置
// ---------------------------------------------------------------------------
void MainWindow::load_config_to_ui() {
    set_window_text_u8(edit_api_key_, config_.ai.api_key);
    set_window_text_u8(edit_base_url_, config_.ai.base_url);
    if (combo_model_) {
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
    if (chk_group_require_at_) {
        SendMessageW(chk_group_require_at_, BM_SETCHECK,
                     config_.onebot.group_require_at ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    std::string exempt;
    for (size_t i = 0; i < config_.onebot.at_exempt_plugins.size(); ++i) {
        exempt += config_.onebot.at_exempt_plugins[i];
        if (i + 1 < config_.onebot.at_exempt_plugins.size()) exempt += ", ";
    }
    set_window_text_u8(edit_at_exempt_, exempt);

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
    config_.onebot.group_require_at = chk_group_require_at_ &&
        (SendMessageW(chk_group_require_at_, BM_GETCHECK, 0, 0) == BST_CHECKED);

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

    config_.ui.theme = theme_index();
    config_.ui.modules.clear();
    for (int i = 0; i < MOD_COUNT; ++i) {
        RECT r{};
        if (mod_wnd_[i]) GetWindowRect(mod_wnd_[i], &r);
        UILayoutItem it;
        it.x = r.left; it.y = r.top;
        it.w = r.right - r.left; it.h = r.bottom - r.top;
        it.visible = mod_open_[i];
        config_.ui.modules.push_back(it);
    }
    config_.save_to_file("config.json");
}

void MainWindow::update_status_ui() {
    if (btn_toggle_bot_) {
        SetWindowTextW(btn_toggle_bot_, is_bot_running_ ? L"停止机器人" : L"启动机器人");
        InvalidateRect(btn_toggle_bot_, NULL, TRUE);
    }
    if (mod_wnd_[MOD_STATUS]) InvalidateRect(mod_wnd_[MOD_STATUS], NULL, TRUE);
    if (h_wnd_) InvalidateRect(h_wnd_, NULL, TRUE);
}

void MainWindow::refresh_plugin_ui() {
    auto list = PluginManager::get_instance().get_plugin_list();
    int enabled = 0;
    for (const auto& p : list) if (p.enabled) ++enabled;
    std::wstring txt = L"已加载 " + std::to_wstring(list.size()) + L" 个插件，" +
                       std::to_wstring(enabled) + L" 个启用中";
    if (label_plugin_summary_) SetWindowTextW(label_plugin_summary_, txt.c_str());
    if (mod_wnd_[MOD_PLUGIN]) InvalidateRect(mod_wnd_[MOD_PLUGIN], NULL, TRUE);
}

// ---------------------------------------------------------------------------
// 行为
// ---------------------------------------------------------------------------
void MainWindow::action_toggle_bot() {
    if (is_bot_running_) {
        if (bot_client_) { bot_client_->stop(); bot_client_.reset(); }
        is_bot_running_ = false;
        update_status_ui();
        log("机器人已停止");
    } else {
        save_ui_to_config();
        if (config_.ai.api_key.empty()) {
            show_module(MOD_AI, true);
            SetFocus(edit_api_key_);
            MessageBoxW(h_wnd_, L"请先在「AI 接口配置」窗口填写 API Key。", L"缺少配置", MB_OK | MB_ICONWARNING);
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

void MainWindow::action_save_config() {
    save_ui_to_config();
    log("配置已保存到 config.json");
    MessageBoxW(h_wnd_, L"配置已保存。", L"保存成功", MB_OK | MB_ICONINFORMATION);
}

void MainWindow::action_test_api() {
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
// 菜单窗口过程
// ---------------------------------------------------------------------------
LRESULT CALLBACK MainWindow::launcher_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = (MainWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    const ThemePalette& t = theme();

    switch (uMsg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        self = (MainWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->init_launcher(hwnd);
        return 0;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, self ? self->brush_win_bg_ : (HBRUSH)GetStockObject(WHITE_BRUSH));
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (self) self->paint_launcher(hdc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE: {
        if (self && self->btn_theme_ && wParam != SIZE_MINIMIZED) {
            int w = LOWORD(lParam), h = HIWORD(lParam);
            SetWindowPos(self->btn_theme_, NULL, 16, h - 44, w - 32, 34, SWP_NOZORDER);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!self) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int row = self->launcher_row_at(pt);
        if (row != self->hover_row_) {
            self->hover_row_ = row;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (self) {
            self->hover_row_ = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONUP: {
        if (!self) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int row = self->launcher_row_at(pt);
        if (row >= 0) {
            self->toggle_module(row);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        break;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (!self || dis->CtlID != IDC_BTN_THEME) break;
        bool hover = self->hovered_.count(IDC_BTN_THEME) > 0;
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        COLORREF bg = pressed ? t.border : (hover ? t.field_bg : t.card_bg);
        ui_fill_round(dis->hDC, dis->rcItem, 9, bg, t.border);
        wchar_t txt[64] = {};
        GetWindowTextW(dis->hwndItem, txt, 64);
        HGDIOBJ old = SelectObject(dis->hDC, self->font_body_bold_);
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, t.primary);
        DrawTextW(dis->hDC, txt, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dis->hDC, old);
        return TRUE;
    }

    case WM_USER_LOG: {
        if (!self) return 0;
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

        for (const auto& m : batch) {
            self->append_log_text(prefix.str() + utf8_to_wstring(m) + L"\r\n", m);
            if (m.find("[BotClient] -> 回复成功") != std::string::npos) {
                self->total_msg_handled_++;
            }
        }
        self->update_status_ui();
        return 0;
    }

    case WM_COMMAND:
        if (self && LOWORD(wParam) == IDC_BTN_THEME) {
            self->apply_theme((theme_index() + 1) % kThemeCount, true);
            self->log(std::string("主题已切换: ") + wstring_to_utf8(std::wstring(theme().name)));
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// 模块窗口过程
// ---------------------------------------------------------------------------
LRESULT CALLBACK MainWindow::module_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ModuleCtx* ctx = (ModuleCtx*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    MainWindow* self = ctx ? ctx->owner : nullptr;
    const ThemePalette& t = theme();

    switch (uMsg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        ctx = (ModuleCtx*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);
        return 0;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, self ? self->brush_card_bg_ : (HBRUSH)GetStockObject(WHITE_BRUSH));
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (self && ctx) self->paint_module_window(ctx->kind, hdc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        if (self && ctx && wParam != SIZE_MINIMIZED) self->relayout_module(ctx->kind);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        if (ctx) {
            int mw = 330, mh = 220;
            if (ctx->kind == MainWindow::MOD_AI) { mw = 360; mh = 430; }
            else if (ctx->kind == MainWindow::MOD_QQ) { mw = 360; mh = 380; }
            else if (ctx->kind == MainWindow::MOD_LOG) { mw = 380; mh = 240; }
            else if (ctx->kind == MainWindow::MOD_ACTION) { mw = 320; mh = 300; }
            else if (ctx->kind == MainWindow::MOD_PLUGIN) { mw = 340; mh = 260; }
            mmi->ptMinTrackSize.x = mw;
            mmi->ptMinTrackSize.y = mh;
        }
        return 0;
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

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (!self) break;
        bool hover = self->hovered_.count((int)dis->CtlID) > 0;
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
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

    case WM_COMMAND: {
        if (!self) break;
        switch (LOWORD(wParam)) {
        case IDC_BTN_TOGGLE_BOT: self->action_toggle_bot(); break;
        case IDC_BTN_SAVE_CONFIG: self->action_save_config(); break;
        case IDC_BTN_TEST_API: self->action_test_api(); break;
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
        }
        return 0;
    }

    case WM_CLOSE:
        if (self && ctx) {
            self->show_module(ctx->kind, false);
            return 0;
        }
        break;

    case WM_NCDESTROY:
        if (self && ctx && ctx->kind >= 0 && ctx->kind < MainWindow::MOD_COUNT) {
            self->mod_wnd_[ctx->kind] = nullptr;
        }
        delete ctx;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
