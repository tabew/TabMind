// ===========================================================================
// TabMind · UI v3（健壮性重写）
//
// 相对 v2 修掉的问题：
//   1) 每次重绘都 CreateCompatibleBitmap 分配全尺寸位图 —— 卡片 600x700 时
//      一帧就是 1.7MB 的分配+清零，这是剩余卡顿的主因。改为跨帧复用的缓冲池。
//   2) 拖动中一旦失去鼠标捕获（按 Alt / Esc / 切窗口），卡内控件会永久处于
//      隐藏状态 → 卡片变成空白。已补 WM_CANCELMODE / WM_CAPTURECHANGED 兜底。
//   3) 拖动时 WM_SETCURSOR 会把光标改回箭头 → 闪烁。拖动期间不再改光标。
//   4) TrackMouseEvent 只在 hover 变化时布防一次，第二次移出可能不触发
//      WM_MOUSELEAVE → 侧栏高亮卡死。改为每次离开都重新布防。
//   5) 工作线程直接弹 MessageBoxW（跨线程 UI 调用，可能卡死）。
//      改为 PostMessage 回 UI 线程处理。
//   6) KillTimer 未调用、ModuleCtx 泄漏、log_buf_ 跨消息复用的成员缓冲区。
//   7) 卡片阴影被裁在客户区外，根本看不见 —— 纯白耗，删掉。
//   8) 尺寸/位置没变也重复 SetWindowPos 十几个控件。
// ===========================================================================
#include "ui_window.hpp"
#include "ui_dialogs.hpp"
#include "ui_theme.hpp"
#include "plugin_manager.hpp"
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

#define IDC_LIST_LOG             1301

// 测试结果回传（工作线程 -> UI 线程）
#define WM_APP_TEST_DONE         (WM_APP + 1)

namespace Lay {
    const int SidebarW  = 218;
    const int TitleH    = 32;
    const int Grip      = 16;
    const int Pad       = 14;
    const int RowH      = 30;
    const int GapY      = 16;
    const int DashLeft  = SidebarW + 12;
    const int MenuTop   = 100;
    const int MenuRowH  = 56;
    const int MinCardW  = 240;
    const int MinCardH  = 120;
}

namespace {
    const int kLogRowH     = 19;
    const int kMaxLogLines = 1500;
    const int kLogTrim     = 300;
    const int kStatusTimer = 1;
    const int kPlgRowH     = 26;
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

static bool is_hovered(const std::vector<int>& v, int id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

// 自绘列表里 emoji 会撑坏行距，统一剥掉
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

// 日志级别：0 信息 / 1 AI / 2 成功 / 3 警告 / 4 错误
// 注意这里读的是原始消息（尚未去除 emoji），所以 emoji 也能参与判定。
// emoji 一律用 UTF-8 字节转义写，避免源码编码变化导致失效。
namespace {
const char kErrEmoji[]  = "\xE2\x9D\x8C";  // U+274C
const char kWarnEmoji[] = "\xE2\x9A\xA0";  // U+26A0
const char kOkEmoji[]   = "\xE2\x9C\x85";  // U+2705
}

static int log_level_of(const std::string& raw) {
    auto has = [&](const char* k) { return raw.find(k) != std::string::npos; };
    if (has(kErrEmoji) || has("失败") || has("错误") || has("异常") || has("FAIL")) return 4;
    if (has(kWarnEmoji) || has("未配置") || has("警告")) return 3;
    if (has(kOkEmoji) || has("已上线") || has("已就绪") || has("通过")) return 2;
    if (has("AI") || has("回复") || has("模型")) return 1;
    return 0;
}

static COLORREF log_color_for_level(int lv) {
    const ThemePalette& t = theme();
    switch (lv) {
    case 4: return t.log_err;
    case 3: return t.log_warn;
    case 2: return t.log_ok;
    case 1: return t.log_ai;
    default: return t.log_info;
    }
}

static const wchar_t* kMenu[6] = {
    L"运行状态", L"AI 接口配置", L"OneBot / QQ 接入", L"扩展插件", L"快捷操作", L"运行日志"
};

static HCURSOR s_cur_arrow = nullptr;
static HCURSOR s_cur_sizeall = nullptr;
static HCURSOR s_cur_sizenwse = nullptr;
static HCURSOR s_cur_hand = nullptr;

static void ensure_cursors() {
    if (!s_cur_arrow)    s_cur_arrow    = LoadCursor(NULL, IDC_ARROW);
    if (!s_cur_sizeall)  s_cur_sizeall  = LoadCursor(NULL, IDC_SIZEALL);
    if (!s_cur_sizenwse) s_cur_sizenwse= LoadCursor(NULL, IDC_SIZENWSE);
    if (!s_cur_hand)     s_cur_hand    = LoadCursor(NULL, IDC_HAND);
}

// ---------------------------------------------------------------------------
// 双缓冲池
// v2 每次重绘都 CreateCompatibleBitmap + DeleteObject，卡片大一点就是每帧
// 几 MB 的分配/清零。这里按需扩容、跨帧复用，绘制路径零分配。
// ---------------------------------------------------------------------------
struct BufSlot {
    HDC     hdc  = nullptr;
    HBITMAP bmp  = nullptr;
    int     w    = 0;
    int     h    = 0;
    bool    used = false;
};

static BufSlot g_bufs[4];

struct PaintBuf {
    BufSlot* slot = nullptr;
    int l = 0, t = 0, r = 0, b = 0;

    HDC mem_dc() const { return slot ? slot->hdc : nullptr; }

    bool begin(HDC hdc, const RECT& rc) {
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0 || !hdc) return false;
        slot = nullptr;
        for (BufSlot& s : g_bufs) {
            if (!s.used) { slot = &s; break; }
        }
        if (!slot) return false;

        if (!slot->hdc) {
            slot->hdc = CreateCompatibleDC(hdc);
            if (!slot->hdc) { slot = nullptr; return false; }
        }
        if (!slot->bmp || slot->w < w || slot->h < h) {
            HBITMAP nb = CreateCompatibleBitmap(hdc, w, h);
            if (!nb) return false;
            if (slot->bmp) DeleteObject(slot->bmp);
            slot->bmp = nb;
            slot->w = w;
            slot->h = h;
        }
        SelectObject(slot->hdc, slot->bmp);
        SetWindowOrgEx(slot->hdc, rc.left, rc.top, NULL);
        slot->used = true;

        l = rc.left; t = rc.top; r = rc.right; b = rc.bottom;
        return true;
    }

    void end(HDC hdc) {
        if (!slot || !hdc) return;
        BitBlt(hdc, l, t, r - l, b - t, slot->hdc, l, t, SRCCOPY);
        slot->used = false;
        slot = nullptr;
    }
};

// 释放全部缓冲池（退出时调用）
static void free_buf_pool() {
    for (BufSlot& s : g_bufs) {
        if (s.bmp)  { DeleteObject(s.bmp);  s.bmp = nullptr; }
        if (s.hdc)  { DeleteDC(s.hdc);      s.hdc = nullptr; }
        s.w = 0; s.h = 0; s.used = false;
    }
}

// ---------------------------------------------------------------------------
// 构造 / GDI
// ---------------------------------------------------------------------------
MainWindow::MainWindow() {}

MainWindow::~MainWindow() {
    HFONT fonts[] = { font_brand_, font_h2_, font_body_, font_body_bold_, font_small_, font_log_ };
    for (HFONT f : fonts) if (f) DeleteObject(f);
    free_buf_pool();
}

MainWindow& MainWindow::get_instance() {
    static MainWindow instance;
    return instance;
}

void MainWindow::init_gdi_resources() {
    ensure_cursors();
    // ANTIALIASED_QUALITY：内存 DC 与屏幕 DC 上表现一致，
    // 不会像 ClearType 那样在双缓冲下发虚。
    auto mk = [](int h, int weight, const wchar_t* face) {
        return CreateFontW(-h, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
    };
    font_brand_     = mk(19, FW_BOLD,   L"Microsoft YaHei UI");
    font_h2_        = mk(14, FW_BOLD,   L"Microsoft YaHei UI");
    font_body_      = mk(14, FW_NORMAL, L"Microsoft YaHei UI");
    font_body_bold_ = mk(14, FW_BOLD,   L"Microsoft YaHei UI");
    font_small_     = mk(12, FW_NORMAL, L"Microsoft YaHei UI");
    font_log_       = mk(13, FW_NORMAL, L"Microsoft YaHei UI");
    ui_gdi().build();
}

const wchar_t* MainWindow::module_title(int kind) {
    if (kind < 0 || kind >= MOD_COUNT) return L"";
    return kMenu[kind];
}

void MainWindow::apply_theme(int index, bool persist) {
    set_theme(index);
    ui_gdi().build();
    const ThemePalette& t = theme();

    if (h_wnd_ == nullptr) {
        if (persist) { config_.ui.theme = theme_index(); config_.save_to_file("config.json"); }
        return;
    }

    if (btn_theme_) SetWindowTextW(btn_theme_, (std::wstring(L"主题 · ") + t.name).c_str());

    const wchar_t* wt = t.dark ? L"DarkMode_Explorer" : L"Explorer";
    for (int k = 0; k < MOD_COUNT; ++k) {
        for (HWND h : mod_ctl_[k]) {
            if (h) { SetWindowTheme(h, wt, NULL); InvalidateRect(h, NULL, FALSE); }
        }
        if (mod_wnd_[k]) InvalidateRect(mod_wnd_[k], NULL, FALSE);
    }
    RedrawWindow(h_wnd_, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);

    if (persist) {
        config_.ui.theme = theme_index();
        config_.save_to_file("config.json");
    }
}

LRESULT CALLBACK MainWindow::button_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                                 UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    MainWindow* self = (MainWindow*)dwRefData;
    if (!self) return DefSubclassProc(hwnd, uMsg, wParam, lParam);
    switch (uMsg) {
    case WM_ERASEBKGND:
        return 1;                     // 全部在 WM_PAINT 里画，避免先擦后画的闪烁
    case WM_MOUSEMOVE: {
        int id = (int)uIdSubclass;
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        if (!is_hovered(self->hovered_, id)) {
            self->hovered_.push_back(id);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    }
    case WM_MOUSELEAVE: {
        auto& v = self->hovered_;
        v.erase(std::remove(v.begin(), v.end(), (int)uIdSubclass), v.end());
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    }
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
    set_theme(config_.ui.theme);
    ui_gdi().build();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWindow::main_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TabMind_Main";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = ui_gdi().br_win;
    if (!RegisterClassW(&wc)) return false;

    WNDCLASSW wm = {};
    wm.lpfnWndProc = MainWindow::module_proc;
    wm.hInstance = hInstance;
    wm.lpszClassName = L"TabMind_Card";
    wm.hCursor = LoadCursor(NULL, IDC_ARROW);
    wm.hbrBackground = ui_gdi().br_win;
    if (!RegisterClassW(&wm)) return false;

    RECT wa = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int win_w = 1200, win_h = 780;
    int max_w = (wa.right - wa.left) - 24;
    int max_h = (wa.bottom - wa.top) - 24;
    if (max_w < 900) max_w = 900;
    if (max_h < 620) max_h = 620;
    if (win_w > max_w) win_w = max_w;
    if (win_h > max_h) win_h = max_h;
    int pos_x = wa.left + ((wa.right - wa.left) - win_w) / 2;
    int pos_y = wa.top + ((wa.bottom - wa.top) - win_h) / 2;

    h_wnd_ = CreateWindowExW(0, L"TabMind_Main", L"TabMind · 智能聊天机器人管理终端",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             pos_x, pos_y, win_w, win_h, NULL, NULL, hInstance, this);
    if (!h_wnd_) return false;

    create_module_windows();
    load_config_to_ui();
    update_status_ui();
    PluginManager::get_instance().load_plugins("plugins");
    refresh_plugin_ui();
    SetTimer(h_wnd_, kStatusTimer, 400, NULL);

    ShowWindow(h_wnd_, nCmdShow);
    UpdateWindow(h_wnd_);

    log("TabMind 已就绪 · 左侧菜单可开关各功能卡片");
    log("OneBot 服务端: " + config_.onebot.ws_url);

    if (!config_.ai.api_key.empty()) {
        log("正在连接 OneBot WebSocket ...");
        bot_client_ = std::make_unique<BotClient>(config_);
        bot_client_->start();
        is_bot_running_ = true;
        update_status_ui();
        log("机器人已上线，正在监听消息");
    } else {
        log("未配置 API Key，请打开「AI 接口配置」卡片填写");
    }
    return true;
}

void MainWindow::init_sidebar(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int y = rc.bottom - 44;
    if (y < 120) y = 120;
    btn_theme_ = CreateWindowW(L"BUTTON", (std::wstring(L"主题 · ") + theme().name).c_str(),
                               WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                               14, y, (Lay::SidebarW - 28), 34, hwnd,
                               (HMENU)(INT_PTR)IDC_BTN_THEME, h_instance_, NULL);
    if (btn_theme_) {
        SetWindowSubclass(btn_theme_, MainWindow::button_subclass_proc, IDC_BTN_THEME,
                          (DWORD_PTR)this);
    }
}

// ---------------------------------------------------------------------------
// 主窗口绘制
// ---------------------------------------------------------------------------
void MainWindow::paint_main(HDC hdc, const RECT& rc, const RECT& rcPaint) {
    const ThemePalette& t = theme();
    UiGdiCache& g = ui_gdi();

    FillRect(hdc, &rcPaint, g.br_win);

    RECT side = { 0, 0, Lay::SidebarW, rc.bottom };
    RECT inter = {};
    if (!IntersectRect(&inter, &side, &rcPaint)) return;   // 侧栏无需重绘

    FillRect(hdc, &side, g.br_card);
    {
        HGDIOBJ op = SelectObject(hdc, g.pen_border_soft);
        MoveToEx(hdc, Lay::SidebarW, 0, NULL);
        LineTo(hdc, Lay::SidebarW, rc.bottom);
        SelectObject(hdc, op);
    }

    // 品牌
    RECT rcLogo = { 14, 14, 46, 46 };
    ui_round(hdc, rcLogo, 9, 9, g.br_primary, g.pen_null);
    {
        HGDIOBJ old = SelectObject(hdc, font_brand_);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        RECT tr = rcLogo;
        DrawTextW(hdc, L"T", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SetTextColor(hdc, t.text_main);
        RECT tr1 = { 54, 13, Lay::SidebarW - 10, 38 };
        DrawTextW(hdc, L"TabMind", -1, &tr1, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(hdc, font_small_);
        SetTextColor(hdc, t.text_sub);
        RECT tr2 = { 56, 36, Lay::SidebarW - 8, 54 };
        DrawTextW(hdc, L"AI Agent Console", -1, &tr2, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, old);
    }

    // 运行状态条
    {
        bool run = is_bot_running_;
        RECT rcPill = { 14, 58, Lay::SidebarW - 14, 86 };
        ui_round(hdc, rcPill, 14, 14, run ? g.br_success_soft : g.br_danger_soft, g.pen_null);
        {
            HGDIOBJ ob = SelectObject(hdc, run ? g.br_success : g.br_danger);
            HGDIOBJ op = SelectObject(hdc, g.pen_null);
            Ellipse(hdc, 26, 68, 36, 78);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
        }
        HGDIOBJ old = SelectObject(hdc, font_small_);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, run ? t.success_text : t.danger);
        RECT tr = { 44, 58, Lay::SidebarW - 18, 86 };
        std::wstring s = run ? L"运行中" : L"已停止";
        s += L" · 已回复 " + std::to_wstring(total_msg_handled_);
        DrawTextW(hdc, s.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, old);
    }

    // 菜单行
    for (int i = 0; i < MOD_COUNT; ++i) {
        RECT row = sidebar_row_rect(i);
        bool open = mod_open_[i];
        bool hover = (hover_row_ == i);
        if (open || hover) {
            ui_round(hdc, row, 9, 9,
                     hover ? g.br_primary_soft : g.br_card,
                     hover ? g.pen_primary : g.pen_border);
        }
        if (open) {
            RECT bar = { row.left + 2, row.top + 12, row.left + 5, row.bottom - 12 };
            ui_round(hdc, bar, 2, 2, g.br_primary, g.pen_null);
        }
        HGDIOBJ old = SelectObject(hdc, (open || hover) ? font_body_bold_ : font_body_);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, open ? t.primary : t.text_main);
        RECT tr = { row.left + 14, row.top, row.right - 26, row.bottom };
        DrawTextW(hdc, kMenu[i], -1, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (open) {
            SetTextColor(hdc, t.primary);
            RECT tr2 = { row.right - 26, row.top, row.right - 8, row.bottom };
            DrawTextW(hdc, L"●", -1, &tr2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        SelectObject(hdc, old);
    }

    // 底部提示
    {
        HGDIOBJ old = SelectObject(hdc, font_small_);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, t.text_faint);
        RECT tr = { 14, rc.bottom - 92, Lay::SidebarW - 14, rc.bottom - 76 };
        DrawTextW(hdc, L"拖标题移动 · 右下角缩放", -1, &tr,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(hdc, old);
    }
}

RECT MainWindow::sidebar_row_rect(int row) const {
    return RECT{ 12, Lay::MenuTop + row * Lay::MenuRowH,
                 Lay::SidebarW - 12, Lay::MenuTop + row * Lay::MenuRowH + Lay::MenuRowH - 6 };
}

int MainWindow::sidebar_row_at(POINT pt) const {
    if (pt.x < 12 || pt.x >= Lay::SidebarW - 12) return -1;
    for (int i = 0; i < MOD_COUNT; ++i) {
        RECT r = sidebar_row_rect(i);
        if (pt.y >= r.top && pt.y < r.bottom) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// 卡片
// ---------------------------------------------------------------------------
RECT MainWindow::default_module_rect(int kind) const {
    RECT cr = { 0, 0, 1200, 760 };
    if (h_wnd_) GetClientRect(h_wnd_, &cr);
    int x0 = Lay::DashLeft, y0 = 12;
    int avail_w = cr.right - x0 - 12;
    int avail_h = cr.bottom - 24;
    if (avail_w < 300) avail_w = 300;
    if (avail_h < 200) avail_h = 200;
    int col = avail_w / 3 - 10;
    if (col < 260) col = 260;

    auto mk = [&](int x, int y, int w, int h) { return RECT{ x, y, x + w, y + h }; };

    switch (kind) {
    case MOD_STATUS: return mk(x0, y0, col, 122);
    case MOD_AI:     return mk(x0, y0 + 132, col, avail_h - 132);
    case MOD_PLUGIN: return mk(x0 + col + 10, y0, col, 250);
    case MOD_QQ:     return mk(x0 + col + 10, y0 + 260, col, avail_h - 260);
    case MOD_ACTION: return mk(x0 + (col + 10) * 2, y0, col, 250);
    case MOD_LOG:    return mk(x0 + (col + 10) * 2, y0 + 260, col, avail_h - 260);
    default:         return mk(x0, y0, 300, 200);
    }
}

void MainWindow::create_module_windows() {
    for (int k = 0; k < MOD_COUNT; ++k) create_module(k);
}

void MainWindow::create_module(int kind) {
    if (kind < 0 || kind >= MOD_COUNT || mod_wnd_[kind]) return;

    bool has_cfg = (config_.ui.modules.size() == (size_t)MOD_COUNT);
    RECT rc = default_module_rect(kind);
    if (has_cfg) {
        const UILayoutItem& it = config_.ui.modules[kind];
        if (it.w > 200 && it.h > 120) rc = RECT{ it.x, it.y, it.x + it.w, it.y + it.h };
        mod_open_[kind] = it.visible;
    } else {
        mod_open_[kind] = true;
    }
    mod_rect_[kind] = rc;
    clamp_module(kind);

    ModuleCtx* ctx = new (std::nothrow) ModuleCtx{ this, kind };
    if (!ctx) return;

    mod_wnd_[kind] = CreateWindowExW(0, L"TabMind_Card", module_title(kind),
                                     WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                     mod_rect_[kind].left, mod_rect_[kind].top,
                                     mod_rect_[kind].right - mod_rect_[kind].left,
                                     mod_rect_[kind].bottom - mod_rect_[kind].top,
                                     h_wnd_, NULL, h_instance_, ctx);
    if (!mod_wnd_[kind]) { delete ctx; return; }

    HWND p = mod_wnd_[kind];
    auto mk_label = [&](const wchar_t* text, HFONT font) {
        HWND h = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
                               -3000, -3000, 10, 10, p, NULL, h_instance_, NULL);
        if (h) SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
        return h;
    };
    auto mk_edit = [&](int id, DWORD style) {
        HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | style,
                                 -3000, -3000, 10, 10, p, (HMENU)(INT_PTR)id, h_instance_, NULL);
        if (e) {
            SendMessageW(e, WM_SETFONT, (WPARAM)font_body_, TRUE);
            SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
        }
        return e;
    };
    auto mk_button = [&](int id, const wchar_t* text) {
        HWND b = CreateWindowW(L"BUTTON", text,
                               WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                               -3000, -3000, 10, 10, p, (HMENU)(INT_PTR)id, h_instance_, NULL);
        if (b) SetWindowSubclass(b, MainWindow::button_subclass_proc, id, (DWORD_PTR)this);
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

        combo_model_ = CreateWindowW(L"COMBOBOX", L"",
                                     WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                     -3000, -3000, 10, 200, p,
                                     (HMENU)(INT_PTR)IDC_COMBO_MODEL, h_instance_, NULL);
        if (combo_model_) {
            SendMessageW(combo_model_, WM_SETFONT, (WPARAM)font_body_, TRUE);
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
            -3000, -3000, 10, 10, p, (HMENU)(INT_PTR)IDC_EDIT_SYSTEM_PROMPT, h_instance_, NULL);
        if (edit_system_prompt_) {
            SendMessageW(edit_system_prompt_, WM_SETFONT, (WPARAM)font_body_, TRUE);
            SendMessageW(edit_system_prompt_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                                         MAKELPARAM(8, 8));
        }
        mod_ctl_[kind].push_back(edit_system_prompt_);
    } else if (kind == MOD_QQ) {
        mod_ctl_[kind].push_back(mk_label(L"OneBot WebSocket 地址", font_small_));
        edit_ws_url_ = mk_edit(IDC_EDIT_WS_URL, 0);
        mod_ctl_[kind].push_back(edit_ws_url_);
        mod_ctl_[kind].push_back(mk_label(L"机器人 QQ 号", font_small_));
        mod_ctl_[kind].push_back(mk_label(L"Access Token", font_small_));
        edit_bot_qq_ = mk_edit(IDC_EDIT_BOT_QQ, ES_NUMBER);
        mod_ctl_[kind].push_back(edit_bot_qq_);
        edit_access_token_ = mk_edit(IDC_EDIT_ACCESS_TOKEN, 0);
        mod_ctl_[kind].push_back(edit_access_token_);
        mod_ctl_[kind].push_back(mk_label(L"触发前缀词（逗号分隔）", font_small_));
        edit_prefixes_ = mk_edit(IDC_EDIT_PREFIXES, 0);
        mod_ctl_[kind].push_back(edit_prefixes_);

        chk_group_require_at_ = CreateWindowW(L"BUTTON", L"群聊必须 @机器人 或命中前缀",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, -3000, -3000, 10, 10, p,
            (HMENU)(INT_PTR)IDC_CHK_REQUIRE_AT, h_instance_, NULL);
        if (chk_group_require_at_) {
            SendMessageW(chk_group_require_at_, WM_SETFONT, (WPARAM)font_body_, TRUE);
        }
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
        btn_reset_layout_ = mk_button(IDC_BTN_RESET_LAYOUT, L"重置布局");
        mod_ctl_[kind].push_back(btn_reset_layout_);
    } else if (kind == MOD_LOG) {
        btn_clear_log_ = mk_button(IDC_BTN_CLEAR_LOG, L"清空");
        mod_ctl_[kind].push_back(btn_clear_log_);

        // 自绘 LISTBOX 取代 RichEdit：只画可见行，追加是 O(1) 的尾部插入
        list_log_ = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER |
            LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
            -3000, -3000, 10, 10, p, (HMENU)(INT_PTR)IDC_LIST_LOG, h_instance_, NULL);
        if (list_log_) {
            SendMessageW(list_log_, WM_SETFONT, (WPARAM)font_log_, TRUE);
            SendMessageW(list_log_, LB_SETITEMHEIGHT, 0, MAKELPARAM(kLogRowH, 0));
            SendMessageW(list_log_, LB_INITSTORAGE, (WPARAM)kMaxLogLines,
                         (LPARAM)(kMaxLogLines * 96));
        }
        mod_ctl_[kind].push_back(list_log_);
    }

    relayout_module(kind);
    ShowWindow(mod_wnd_[kind], mod_open_[kind] ? SW_SHOW : SW_HIDE);
}

void MainWindow::show_module(int kind, bool open) {
    if (kind < 0 || kind >= MOD_COUNT || !mod_wnd_[kind]) return;
    mod_open_[kind] = open;
    ShowWindow(mod_wnd_[kind], open ? SW_SHOW : SW_HIDE);
    if (open) bring_to_front(kind);
    if (h_wnd_) InvalidateRect(h_wnd_, NULL, FALSE);
    save_layout();
}

void MainWindow::apply_z_order() {
    HDWP hdwp = BeginDeferWindowPos(MOD_COUNT);
    if (!hdwp) return;
    for (int i = 0; i < MOD_COUNT; ++i) {
        HWND h = mod_wnd_[z_order_[i]];
        if (h) hdwp = DeferWindowPos(hdwp, h, HWND_TOP, 0, 0, 0, 0,
                                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
    }
    EndDeferWindowPos(hdwp);
    // 换序后原本被盖住的区域要重画
    if (h_wnd_) InvalidateRect(h_wnd_, NULL, FALSE);
}

// 惰性置顶：已经在最上层就什么都不做，避免每次点击都全卡片重排
void MainWindow::bring_to_front(int kind) {
    if (kind < 0 || kind >= MOD_COUNT || !mod_wnd_[kind]) return;
    if (z_order_[MOD_COUNT - 1] == kind) return;
    int pos = -1;
    for (int i = 0; i < MOD_COUNT; ++i) if (z_order_[i] == kind) { pos = i; break; }
    if (pos < 0) return;
    for (int i = pos; i < MOD_COUNT - 1; ++i) z_order_[i] = z_order_[i + 1];
    z_order_[MOD_COUNT - 1] = kind;
    apply_z_order();
}

// 批量显示/隐藏卡内控件：一次 DeferWindowPos 完成
void MainWindow::set_card_controls_visible(int kind, bool show) {
    auto& v = mod_ctl_[kind];
    if (v.empty()) return;
    HDWP hdwp = BeginDeferWindowPos((int)v.size());
    if (!hdwp) return;
    for (HWND h : v) {
        if (!h) continue;
        hdwp = DeferWindowPos(hdwp, h, NULL, 0, 0, 0, 0,
                              SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                              SWP_NOREDRAW | (show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
    EndDeferWindowPos(hdwp);
}

void MainWindow::clamp_module(int kind) {
    if (kind < 0 || kind >= MOD_COUNT) return;
    RECT cr = { 0, 0, 900, 620 };
    if (h_wnd_) GetClientRect(h_wnd_, &cr);
    RECT& r = mod_rect_[kind];
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w < Lay::MinCardW) w = Lay::MinCardW;
    if (h < Lay::MinCardH) h = Lay::MinCardH;
    int min_x = Lay::DashLeft, min_y = 8;
    int max_x = cr.right - 8 - w;
    int max_y = cr.bottom - 8 - h;
    if (max_x < min_x) max_x = min_x;
    if (max_y < min_y) max_y = min_y;
    if (r.left < min_x) r.left = min_x;
    if (r.top < min_y) r.top = min_y;
    if (r.left > max_x) r.left = max_x;
    if (r.top > max_y) r.top = max_y;
    r.right = r.left + w;
    r.bottom = r.top + h;
    if (mod_wnd_[kind]) {
        SetWindowPos(mod_wnd_[kind], NULL, r.left, r.top, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    }
}

void MainWindow::save_layout() {
    config_.ui.modules.clear();
    for (int i = 0; i < MOD_COUNT; ++i) {
        UILayoutItem it;
        it.x = mod_rect_[i].left;
        it.y = mod_rect_[i].top;
        it.w = mod_rect_[i].right - mod_rect_[i].left;
        it.h = mod_rect_[i].bottom - mod_rect_[i].top;
        it.visible = mod_open_[i];
        config_.ui.modules.push_back(it);
    }
    config_.ui.theme = theme_index();
    config_.save_to_file("config.json");
}

void MainWindow::relayout_module(int kind) {
    HWND p = mod_wnd_[kind];
    if (!p) return;

    RECT cr;
    GetClientRect(p, &cr);
    int W = cr.right, H = cr.bottom;
    // 卡片太小就不摆控件，避免算出负坐标把控件挪到屏幕外
    if (W < 96 || H < 60) return;

    const int P = Lay::Pad;
    const int top = Lay::TitleH + 8;
    const int rowH = Lay::RowH;
    const int gapY = Lay::GapY;

    // 参数没变就不调 SetWindowPos —— 否则每次主窗口 WM_SIZE 都会把
    // 几十个控件逐个重摆一遍，各触发一次重绘
    auto place = [p](HWND h, int x, int y, int w, int hh) {
        if (!h) return;
        RECT cur;
        if (GetWindowRect(h, &cur)) {
            POINT tl = { cur.left, cur.top };
            POINT br = { cur.right, cur.bottom };
            MapWindowPoints(HWND_DESKTOP, p, &tl, 1);
            MapWindowPoints(HWND_DESKTOP, p, &br, 1);
            if (tl.x == x && tl.y == y && (br.x - tl.x) == w && (br.y - tl.y) == hh) return;
        }
        SetWindowPos(h, NULL, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE);
    };

    if (kind == MOD_AI) {
        auto& c = mod_ctl_[kind];
        int L = P, CW = W - P * 2;
        if (CW < 40) CW = 40;
        int y = top;
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
        place(combo_model_, L, y + 17, cModel, 200);
        place(c[5], L + cModel + 8, y, cTok, 16);
        place(edit_max_tokens_, L + cModel + 8, y + 17, cTok, rowH);
        place(c[6], L + cModel + cTok + 16, y, cRound, 16);
        place(edit_history_rounds_, L + cModel + cTok + 16, y + 17, cRound, rowH);
        y += 17 + rowH + gapY;

        place(c[10], L, y, CW, 16);
        int ph = H - 12 - (y + 17);
        if (ph < 50) ph = 50;
        place(edit_system_prompt_, L, y + 17, CW, ph);
    } else if (kind == MOD_QQ) {
        auto& c = mod_ctl_[kind];
        int L = P, CW = W - P * 2;
        if (CW < 40) CW = 40;
        int y = top;
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
        y += 28;
        place(c[9], L, y, CW, 16);
        place(edit_at_exempt_, L, y + 17, CW, rowH);
    } else if (kind == MOD_PLUGIN) {
        auto& c = mod_ctl_[kind];
        int L = P, CW = W - P * 2;
        if (CW < 40) CW = 40;
        place(c[0], L, top, CW, 30);
        int y = top + 36;
        int bw = (CW - 16) / 3;
        if (bw < 60) bw = 60;
        place(btn_reload_plugins_, L, y, bw, 30);
        place(btn_open_plugin_dir_, L + bw + 8, y, bw, 30);
        place(btn_plugin_mgr_, L + (bw + 8) * 2, y, bw, 30);
    } else if (kind == MOD_ACTION) {
        int L = P, CW = W - P * 2;
        if (CW < 40) CW = 40;
        int y = top;
        place(btn_toggle_bot_, L, y, CW, 42);
        y += 52;
        int half = (CW - 8) / 2;
        place(btn_save_config_, L, y, half, 32);
        place(btn_test_api_, L + half + 8, y, half, 32);
        y += 40;
        place(btn_advanced_, L, y, half, 32);
        place(btn_help_, L + half + 8, y, half, 32);
        y += 40;
        place(btn_reset_layout_, L, y, CW, 30);
    } else if (kind == MOD_LOG) {
        place(btn_clear_log_, W - P - 52, 5, 46, 24);
        int y = top - 2;
        int hh = H - y - P;
        if (hh < 40) hh = 40;
        place(list_log_, P - 6, y, W - (P - 6) * 2, hh);
    }
}

void MainWindow::paint_module_card(int kind, HDC hdc, const RECT& rc, const RECT& rcPaint) {
    const ThemePalette& t = theme();
    UiGdiCache& g = ui_gdi();
    (void)rcPaint;                     // 缓冲已按无效区裁剪，这里按全客户区画

    FillRect(hdc, &rc, g.br_win);
    ui_round(hdc, rc, 12, 12, g.br_card, g.pen_border);

    // 标题
    HGDIOBJ old = SelectObject(hdc, font_h2_);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, t.text_main);
    RECT rcTitle = { Lay::Pad, 6, rc.right - 40, Lay::TitleH };
    DrawTextW(hdc, module_title(kind), -1, &rcTitle,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    // 关闭按钮
    RECT rcClose = { rc.right - 30, 7, rc.right - 10, 27 };
    ui_round(hdc, rcClose, 10, 10, g.br_field, g.pen_null);
    {
        HGDIOBJ op = SelectObject(hdc, g.pen_text_sub);
        MoveToEx(hdc, rcClose.left + 6, rcClose.top + 6, NULL);
        LineTo(hdc, rcClose.right - 6, rcClose.bottom - 6);
        MoveToEx(hdc, rcClose.right - 6, rcClose.top + 6, NULL);
        LineTo(hdc, rcClose.left + 6, rcClose.bottom - 6);
        SelectObject(hdc, op);
    }

    // 缩放手柄
    if (rc.bottom - rc.top > Lay::TitleH + 30) {
        int gl = rc.right - Lay::Grip, gt = rc.bottom - Lay::Grip;
        HGDIOBJ op = SelectObject(hdc, g.pen_text_sub);
        for (int i = 1; i <= 3; ++i) {
            int x0 = gl + i * 4;
            int y0 = gt + i * 4;
            MoveToEx(hdc, x0, rc.bottom - 2, NULL);
            LineTo(hdc, rc.right - 2, y0);
        }
        SelectObject(hdc, op);
    }

    if (kind == MOD_STATUS) {
        int L = Lay::Pad, W = rc.right - Lay::Pad * 2;
        int y = Lay::TitleH + 6;
        int w1 = W * 30 / 100, w2 = W * 42 / 100, w3 = W - w1 - w2 - 12;
        if (w3 < 50) w3 = 50;
        RECT r1 = { L, y, L + w1, y + 30 };
        RECT r2 = { L + w1 + 6, y, L + w1 + 6 + w2, y + 30 };
        RECT r3 = { L + w1 + w2 + 12, y, L + w1 + w2 + 12 + w3, y + 30 };
        bool run = is_bot_running_;
        ui_pill(hdc, r1, run ? g.br_success_soft : g.br_danger_soft, g.pen_null,
                run ? t.success_text : t.danger,
                run ? L"● 运行中" : L"● 已停止", font_small_);
        ui_pill(hdc, r2, g.br_primary_soft, g.pen_null, t.primary,
                L"模型 " + utf8_to_wstring(config_.ai.model), font_small_);
        ui_pill(hdc, r3, g.br_field, g.pen_null, t.text_main,
                L"已回复 " + std::to_wstring(total_msg_handled_), font_small_);
    } else if (kind == MOD_PLUGIN) {
        int L = Lay::Pad, W = rc.right - Lay::Pad * 2;
        int list_top = Lay::TitleH + 78;
        int avail = rc.bottom - Lay::Pad - 16 - list_top;
        if (avail < kPlgRowH) return;
        int max_rows = avail / kPlgRowH;
        if (max_rows < 1) max_rows = 1;

        HGDIOBJ old2 = SelectObject(hdc, font_small_);
        SetBkMode(hdc, TRANSPARENT);
        if (plugin_cache_.empty()) {
            SetTextColor(hdc, t.text_faint);
            RECT rcE = { L, list_top, L + W, list_top + 20 };
            DrawTextW(hdc, L"暂无插件 · 把 DLL 放进 plugins 目录", -1, &rcE,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else {
            int shown = 0;
            for (const auto& pl : plugin_cache_) {
                if (shown >= max_rows) break;
                int y = list_top + shown * kPlgRowH;
                RECT rcRow = { L, y, L + W, y + kPlgRowH - 4 };
                ui_round(hdc, rcRow, 7, 7, g.br_field, g.pen_null);
                {
                    HGDIOBJ ob = SelectObject(hdc, pl.enabled ? g.br_success : g.br_text_faint);
                    HGDIOBJ op = SelectObject(hdc, g.pen_null);
                    Ellipse(hdc, L + 8, y + 7, L + 14, y + 13);
                    SelectObject(hdc, ob);
                    SelectObject(hdc, op);
                }
                SelectObject(hdc, font_body_);
                SetTextColor(hdc, pl.enabled ? t.text_main : t.text_sub);
                RECT rcName = { L + 20, y + 1, L + W - 56, y + kPlgRowH - 4 };
                DrawTextW(hdc, utf8_to_wstring(pl.name).c_str(), -1, &rcName,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                SelectObject(hdc, font_small_);
                SetTextColor(hdc, t.text_faint);
                RECT rcVer = { L + W - 54, y + 1, L + W - 6, y + kPlgRowH - 4 };
                DrawTextW(hdc, utf8_to_wstring("v" + pl.version).c_str(), -1, &rcVer,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                ++shown;
            }
            if ((int)plugin_cache_.size() > shown) {
                RECT rcMore = { L, list_top + shown * kPlgRowH, L + W,
                                list_top + shown * kPlgRowH + 16 };
                SetTextColor(hdc, t.text_faint);
                std::wstring more = L"还有 " + std::to_wstring(plugin_cache_.size() - shown) + L" 个";
                DrawTextW(hdc, more.c_str(), -1, &rcMore, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            }
        }
        SelectObject(hdc, old2);
    }
    SelectObject(hdc, old);
}

// ---------------------------------------------------------------------------
// 配置读写
// ---------------------------------------------------------------------------
void MainWindow::load_config_to_ui() {
    set_window_text_u8(edit_api_key_, config_.ai.api_key);
    set_window_text_u8(edit_base_url_, config_.ai.base_url);
    if (combo_model_) {
        std::wstring want = utf8_to_wstring(config_.ai.model);
        int idx = (int)SendMessageW(combo_model_, CB_FINDSTRINGEXACT, (WPARAM)-1,
                                    (LPARAM)want.c_str());
        if (idx == CB_ERR && !want.empty()) {
            SendMessageW(combo_model_, CB_ADDSTRING, 0, (LPARAM)want.c_str());
            idx = (int)SendMessageW(combo_model_, CB_GETCOUNT, 0, 0) - 1;
        }
        if (idx != CB_ERR && idx >= 0) SendMessageW(combo_model_, CB_SETCURSEL, idx, 0);
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

    save_layout();
}

// 只重画真正会变的两小块：侧栏状态条 + 运行状态卡片
void MainWindow::update_status_ui() {
    if (h_wnd_ == nullptr) { status_dirty_ = false; return; }
    if (btn_toggle_bot_) {
        SetWindowTextW(btn_toggle_bot_, is_bot_running_ ? L"停止机器人" : L"启动机器人");
        InvalidateRect(btn_toggle_bot_, NULL, FALSE);
    }
    if (mod_wnd_[MOD_STATUS]) InvalidateRect(mod_wnd_[MOD_STATUS], NULL, FALSE);
    {
        RECT pill = { 0, 54, Lay::SidebarW, 90 };
        InvalidateRect(h_wnd_, &pill, FALSE);
    }
    status_dirty_ = false;
}

void MainWindow::refresh_plugin_ui() {
    plugin_cache_ = PluginManager::get_instance().get_plugin_list();
    int enabled = 0;
    for (const auto& p : plugin_cache_) if (p.enabled) ++enabled;
    std::wstring txt = L"已加载 " + std::to_wstring(plugin_cache_.size()) + L" 个插件，" +
                       std::to_wstring(enabled) + L" 个启用中";
    if (label_plugin_summary_) SetWindowTextW(label_plugin_summary_, txt.c_str());
    if (mod_wnd_[MOD_PLUGIN]) InvalidateRect(mod_wnd_[MOD_PLUGIN], NULL, FALSE);
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
            if (edit_api_key_) SetFocus(edit_api_key_);
            if (h_wnd_) {
                MessageBoxW(h_wnd_, L"请先在「AI 接口配置」卡片填写 API Key。",
                            L"缺少配置", MB_OK | MB_ICONWARNING);
            }
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
    if (h_wnd_) MessageBoxW(h_wnd_, L"配置已保存。", L"保存成功", MB_OK | MB_ICONINFORMATION);
}

// 测试结果：从工作线程回传，保证弹窗只发生在 UI 线程
struct TestResultPayload {
    bool ok;
    std::string detail;
};

void MainWindow::action_test_api() {
    save_ui_to_config();
    log("正在测试 AI 接口: " + config_.ai.base_url + " ...");

    std::thread([](MainWindow* self) {
        TestResultPayload* p = new (std::nothrow) TestResultPayload();
        if (!p) return;
        p->ok = false;
        try {
            AIClient client(self->config_.ai);
            std::string reply = client.chat("test_ping", "请回复两个字：收到");
            p->ok = reply.find("【AI") == std::string::npos;
            p->detail = reply;
        } catch (const std::exception& e) {
            p->detail = e.what();
        } catch (...) {
            p->detail = "unknown error";
        }
        if (self->h_wnd_) {
            PostMessageW(self->h_wnd_, WM_APP_TEST_DONE, 0, (LPARAM)p);
        } else {
            delete p;
        }
    }, this).detach();
}

void MainWindow::log(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        pending_logs_.push_back(message);
    }
    if (h_wnd_) PostMessageW(h_wnd_, WM_USER_LOG, 0, 0);
}

void MainWindow::log_scroll_bottom() {
    if (!list_log_) return;
    int count = (int)SendMessageW(list_log_, LB_GETCOUNT, 0, 0);
    if (count <= 0) return;
    RECT cr;
    GetClientRect(list_log_, &cr);
    int itemH = (int)SendMessageW(list_log_, LB_GETITEMHEIGHT, 0, 0);
    if (itemH < 1) itemH = kLogRowH;
    int perPage = (cr.bottom - cr.top) / itemH;
    if (perPage < 1) perPage = 1;
    int top = count - perPage;
    if (top < 0) top = 0;
    SendMessageW(list_log_, LB_SETTOPINDEX, (WPARAM)top, 0);
}

void MainWindow::log_update_follow() {
    if (!list_log_) return;
    int count = (int)SendMessageW(list_log_, LB_GETCOUNT, 0, 0);
    int top = (int)SendMessageW(list_log_, LB_GETTOPINDEX, 0, 0);
    RECT cr;
    GetClientRect(list_log_, &cr);
    int itemH = (int)SendMessageW(list_log_, LB_GETITEMHEIGHT, 0, 0);
    if (itemH < 1) itemH = kLogRowH;
    int perPage = (cr.bottom - cr.top) / itemH;
    if (perPage < 1) perPage = 1;
    log_follow_ = (top + perPage >= count);
}

// 拖动被系统强行终止（Alt / Esc / 弹窗 / 切窗口）时调用：
// 把卡内控件恢复显示，否则卡片会永久变成空白
void MainWindow::abort_drag() {
    if (drag_mode_ == 0) return;
    int id = drag_mod_;
    drag_mode_ = 0;
    drag_mod_ = -1;
    if (destroying_ || id < 0 || id >= MOD_COUNT || !mod_wnd_[id]) return;
    set_card_controls_visible(id, true);
    relayout_module(id);
    RedrawWindow(mod_wnd_[id], NULL, NULL,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void MainWindow::run_message_loop() {
    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// ---------------------------------------------------------------------------
// 主窗口过程
// ---------------------------------------------------------------------------
LRESULT CALLBACK MainWindow::main_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = (MainWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    const ThemePalette& t = theme();

    switch (uMsg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        self = (MainWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        if (self) self->init_sidebar(hwnd);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (self) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            PaintBuf pb;
            if (pb.begin(hdc, ps.rcPaint)) {
                self->paint_main(pb.mem_dc(), rc, ps.rcPaint);
                pb.end(hdc);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE: {
        if (self && wParam != SIZE_MINIMIZED) {
            int w = LOWORD(lParam), h = HIWORD(lParam);
            if (self->btn_theme_) {
                SetWindowPos(self->btn_theme_, NULL, 14, h - 44, Lay::SidebarW - 28, 34, SWP_NOZORDER);
            }
            for (int k = 0; k < MOD_COUNT; ++k) {
                if (self->mod_wnd_[k]) { self->clamp_module(k); self->relayout_module(k); }
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 920;
        mmi->ptMinTrackSize.y = 640;
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!self) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int row = self->sidebar_row_at(pt);
        if (row != self->hover_row_) {
            int old_row = self->hover_row_;
            self->hover_row_ = row;
            // 每次变化都重新布防：否则第二次移出窗口可能收不到 WM_MOUSELEAVE
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            if (old_row >= 0 && old_row < MOD_COUNT) {
                RECT r = self->sidebar_row_rect(old_row);
                InvalidateRect(hwnd, &r, FALSE);
            }
            if (row >= 0) {
                RECT r = self->sidebar_row_rect(row);
                InvalidateRect(hwnd, &r, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        if (self && self->hover_row_ >= 0) {
            RECT r = self->sidebar_row_rect(self->hover_row_);
            self->hover_row_ = -1;
            InvalidateRect(hwnd, &r, FALSE);
        }
        // 重新布防，保证下次移出还能收到
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_LBUTTONUP: {
        if (!self) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int row = self->sidebar_row_at(pt);
        if (row >= 0) {
            self->show_module(row, !self->mod_open_[row]);
            RECT r = self->sidebar_row_rect(row);
            InvalidateRect(hwnd, &r, FALSE);
        }
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && s_cur_arrow) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            SetCursor(pt.x < Lay::SidebarW ? s_cur_hand : s_cur_arrow);
            return TRUE;
        }
        break;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (!self || !dis || dis->CtlID != IDC_BTN_THEME) break;
        UiGdiCache& g = ui_gdi();
        bool hover = is_hovered(self->hovered_, IDC_BTN_THEME);
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        HBRUSH bg = pressed ? g.br_border : (hover ? g.br_field : g.br_card);
        ui_round(dis->hDC, dis->rcItem, 9, 9, bg, g.pen_border);
        wchar_t txt[64] = {};
        GetWindowTextW(dis->hwndItem, txt, 64);
        HGDIOBJ old = SelectObject(dis->hDC, self->font_small_);
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, t.primary);
        DrawTextW(dis->hDC, txt, -1, &dis->rcItem,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dis->hDC, old);
        return TRUE;
    }

    case WM_TIMER:
        if (self && wParam == kStatusTimer && self->status_dirty_) self->update_status_ui();
        return 0;

    case WM_COMMAND:
        if (self && LOWORD(wParam) == IDC_BTN_THEME && HIWORD(wParam) == BN_CLICKED) {
            self->apply_theme((theme_index() + 1) % kThemeCount, true);
            self->log(std::string("主题已切换: ") + wstring_to_utf8(std::wstring(theme().name)));
            return 0;
        }
        break;

    case WM_APP_TEST_DONE: {
        if (!self) break;
        TestResultPayload* p = (TestResultPayload*)lParam;
        if (p) {
            if (p->ok) {
                self->log("接口测试通过，模型回复: " + p->detail);
                MessageBoxW(hwnd, L"接口连通，大模型已就绪。", L"测试成功", MB_OK | MB_ICONINFORMATION);
            } else {
                self->log("接口测试失败: " + p->detail);
                MessageBoxW(hwnd, utf8_to_wstring("接口测试失败：\r\n" + p->detail).c_str(),
                            L"测试失败", MB_OK | MB_ICONERROR);
            }
            delete p;
        }
        return 0;
    }

    case WM_USER_LOG: {
        if (!self || !self->h_wnd_) return 0;
        std::vector<std::string> batch;
        {
            std::lock_guard<std::mutex> lock(self->log_mutex_);
            batch.swap(self->pending_logs_);
        }
        if (batch.empty()) return 0;
        if (batch.size() > 400) batch.erase(batch.begin(), batch.end() - 400);

        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now;
        localtime_s(&tm_now, &tt);
        std::wstringstream prefix;
        prefix << L"[" << std::setw(2) << std::setfill(L'0') << tm_now.tm_hour << L":"
               << std::setw(2) << std::setfill(L'0') << tm_now.tm_min << L":"
               << std::setw(2) << std::setfill(L'0') << tm_now.tm_sec << L"]  ";

        HWND lb = self->list_log_;
        if (lb) {
            SendMessageW(lb, WM_SETREDRAW, FALSE, 0);
            for (const auto& m : batch) {
                std::wstring line = prefix.str() + utf8_to_wstring(m);
                strip_emoji(line);
                int idx = (int)SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)line.c_str());
                if (idx >= 0) {
                    SendMessageW(lb, LB_SETITEMDATA, (WPARAM)idx, (LPARAM)log_level_of(m));
                }
                if (m.find("[BotClient] -> 回复成功") != std::string::npos) {
                    self->total_msg_handled_++;
                }
            }
            int count = (int)SendMessageW(lb, LB_GETCOUNT, 0, 0);
            if (count > kMaxLogLines) {
                int del = count - kMaxLogLines + kLogTrim;
                for (int i = 0; i < del; ++i) SendMessageW(lb, LB_DELETESTRING, 0, 0);
            }
            SendMessageW(lb, WM_SETREDRAW, TRUE, 0);
            if (self->log_follow_) self->log_scroll_bottom();
            InvalidateRect(lb, NULL, FALSE);
        } else {
            for (const auto& m : batch) {
                if (m.find("[BotClient] -> 回复成功") != std::string::npos) {
                    self->total_msg_handled_++;
                }
            }
        }
        self->status_dirty_ = true;
        return 0;
    }

    case WM_DESTROY:
        if (self) { self->destroying_ = true; self->abort_drag(); }
        KillTimer(hwnd, kStatusTimer);
        free_buf_pool();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// 卡片子窗口过程
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

    case WM_NCDESTROY:
        // ctx 由 create_module 创建，随窗口销毁释放
        if (ctx) { delete ctx; SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (self && ctx) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            PaintBuf pb;
            if (pb.begin(hdc, ps.rcPaint)) {
                self->paint_module_card(ctx->kind, pb.mem_dc(), rc, ps.rcPaint);
                pb.end(hdc);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        if (self && ctx && wParam != SIZE_MINIMIZED) self->relayout_module(ctx->kind);
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, t.card_bg);
        SetBkMode(hdc, OPAQUE);
        SetTextColor(hdc, t.text_sub);
        return (LRESULT)ui_gdi().br_card;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, t.field_bg);
        SetTextColor(hdc, t.text_main);
        return (LRESULT)ui_gdi().br_field;
    }

    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, t.log_bg);
        return (LRESULT)ui_gdi().br_log;
    }

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
        if (mis && mis->CtlType == ODT_LISTBOX) { mis->itemHeight = kLogRowH; return TRUE; }
        break;
    }

    case WM_MOUSEACTIVATE:
        if (self && ctx) {
            self->bring_to_front(ctx->kind);   // 已置顶时是 no-op
            return MA_ACTIVATE;
        }
        break;

    case WM_SETCURSOR: {
        if (LOWORD(lParam) != HTCLIENT || !s_cur_arrow) break;
        if (self && self->drag_mode_ != 0) {
            SetCursor(self->drag_mode_ == 2 ? s_cur_sizenwse : s_cur_sizeall);
            return TRUE;
        }
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        RECT cr;
        GetClientRect(hwnd, &cr);
        if (pt.x >= cr.right - Lay::Grip && pt.y >= cr.bottom - Lay::Grip) SetCursor(s_cur_sizenwse);
        else if (pt.y < Lay::TitleH) SetCursor(s_cur_sizeall);
        else SetCursor(s_cur_arrow);
        return TRUE;
    }

    case WM_LBUTTONDOWN: {
        if (!self || !ctx) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT cr;
        GetClientRect(hwnd, &cr);

        RECT rcClose = { cr.right - 30, 7, cr.right - 10, 27 };
        if (pt.x >= rcClose.left && pt.x < rcClose.right &&
            pt.y >= rcClose.top && pt.y < rcClose.bottom) {
            self->show_module(ctx->kind, false);
            return 0;
        }
        if (cr.right - cr.left < Lay::MinCardW || cr.bottom - cr.top < Lay::MinCardH) return 0;

        if (pt.x >= cr.right - Lay::Grip && pt.y >= cr.bottom - Lay::Grip) {
            self->drag_mode_ = 2;
        } else if (pt.y < Lay::TitleH) {
            self->drag_mode_ = 1;
        } else {
            break;
        }
        self->drag_mod_ = ctx->kind;
        self->drag_pt_ = pt;
        self->drag_rc0_ = self->mod_rect_[ctx->kind];
        self->bring_to_front(ctx->kind);
        SetCapture(hwnd);
        SetCursor(self->drag_mode_ == 2 ? s_cur_sizenwse : s_cur_sizeall);
        // 拖动期间隐藏卡内控件：只剩 1 个窗口在动，十几个子控件不会各自重绘
        self->set_card_controls_visible(ctx->kind, false);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!self || !ctx || self->drag_mode_ == 0 || self->drag_mod_ != ctx->kind) break;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        POINT delta = { pt.x - self->drag_pt_.x, pt.y - self->drag_pt_.y };

        RECT rc = self->drag_rc0_;
        if (self->drag_mode_ == 1) {
            rc.left += delta.x; rc.right += delta.x;
            rc.top += delta.y;  rc.bottom += delta.y;
        } else {
            rc.right += delta.x; rc.bottom += delta.y;
            if (rc.right - rc.left < Lay::MinCardW) rc.right = rc.left + Lay::MinCardW;
            if (rc.bottom - rc.top < Lay::MinCardH) rc.bottom = rc.top + Lay::MinCardH;
        }

        RECT parent_rc;
        GetClientRect(GetParent(hwnd), &parent_rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (rc.left < Lay::DashLeft) rc.left = Lay::DashLeft;
        if (rc.top < 8) rc.top = 8;
        if (rc.left > parent_rc.right - 8 - w) rc.left = parent_rc.right - 8 - w;
        if (rc.top > parent_rc.bottom - 8 - h) rc.top = parent_rc.bottom - 8 - h;
        rc.right = rc.left + w;
        rc.bottom = rc.top + h;

        RECT old = self->mod_rect_[ctx->kind];
        if (rc.left == old.left && rc.top == old.top &&
            rc.right == old.right && rc.bottom == old.bottom) return 0;
        self->mod_rect_[ctx->kind] = rc;

        // SWP_NOCOPYBITS：强制只重画暴露区域，避免整块拷贝放大开销
        if (self->drag_mode_ == 1) {
            SetWindowPos(hwnd, NULL, rc.left, rc.top, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        } else {
            SetWindowPos(hwnd, NULL, rc.left, rc.top, w, h,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        }
        // 补画卡片让出来的那块背景
        InvalidateRect(GetParent(hwnd), &old, FALSE);
        return 0;
    }

    case WM_CANCELMODE:
        // Alt / Esc / 弹窗 等导致捕获被系统收回：必须还原控件，否则卡片永久空白
        if (self && self->drag_mod_ >= 0) self->abort_drag();
        break;

    case WM_CAPTURECHANGED:
        // 捕获被别的窗口拿走（Alt+Tab 等），同样还原
        if (self && self->drag_mode_ != 0 && (HWND)lParam != hwnd) self->abort_drag();
        break;

    case WM_LBUTTONUP: {
        if (!self || self->drag_mode_ == 0 || self->drag_mod_ != ctx->kind) break;
        int id = self->drag_mod_;
        self->drag_mode_ = 0;
        self->drag_mod_ = -1;
        if (GetCapture() == hwnd) ReleaseCapture();   // 先清状态再释放，避免重复回调

        // 卡片已在目标位置；子控件相对位置不变，一次性批量恢复显示
        self->set_card_controls_visible(id, true);
        self->relayout_module(id);
        if (self->mod_wnd_[id]) {
            RedrawWindow(self->mod_wnd_[id], NULL, NULL,
                         RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        self->save_layout();
        return 0;
    }

    case WM_VSCROLL:
        if (self && (HWND)lParam == self->list_log_) self->log_update_follow();
        break;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (!self || !dis) break;
        UiGdiCache& g = ui_gdi();

        // ---- 日志行：只画可见行，代价与总行数无关 ----
        if (dis->CtlID == IDC_LIST_LOG) {
            if (dis->itemID < 0) {
                FillRect(dis->hDC, &dis->rcItem, g.br_log);
                return TRUE;
            }
            int lv = (int)dis->itemData;
            COLORREF fg = log_color_for_level(lv);
            HBRUSH bg = g.br_log;
            if (dis->itemState & ODS_SELECTED) {
                bg = g.br_primary_soft;
                fg = t.text_main;
            }
            FillRect(dis->hDC, &dis->rcItem, bg);

            // 用栈上缓冲区：不用成员变量，避免消息重入时被改写
            int len = (int)SendMessageW(dis->hwndItem, LB_GETTEXTLEN, (WPARAM)dis->itemID, 0);
            if (len < 0) len = 0;
            if (len > 4000) len = 4000;
            wchar_t tmp[4004];
            tmp[0] = L'\0';
            if (len > 0) {
                SendMessageW(dis->hwndItem, LB_GETTEXT, (WPARAM)dis->itemID, (LPARAM)tmp);
            }

            HGDIOBJ old = SelectObject(dis->hDC, self->font_log_);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, fg);
            RECT tr = dis->rcItem;
            tr.left += 6;
            tr.right -= 4;
            DrawTextW(dis->hDC, tmp, -1, &tr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(dis->hDC, old);
            return TRUE;
        }

        bool hover = is_hovered(self->hovered_, (int)dis->CtlID);
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        wchar_t txt[64] = {};
        GetWindowTextW(dis->hwndItem, txt, 64);

        if (dis->CtlID == IDC_BTN_TOGGLE_BOT) {
            COLORREF bg = self->is_bot_running_ ? t.danger : t.primary;
            if (hover) bg = self->is_bot_running_ ? RGB(220, 38, 38) : t.primary_dark;
            if (pressed) bg = self->is_bot_running_ ? RGB(185, 28, 28) : t.primary_dark;
            HBRUSH hb = CreateSolidBrush(bg);
            ui_round(dis->hDC, dis->rcItem, 9, 9, hb, g.pen_null);
            DeleteObject(hb);
            HGDIOBJ old = SelectObject(dis->hDC, self->font_h2_);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, RGB(255, 255, 255));
            DrawTextW(dis->hDC, txt, -1, &dis->rcItem,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dis->hDC, old);
            return TRUE;
        }

        HBRUSH bg = pressed ? g.br_border : (hover ? g.br_field : g.br_card);
        HPEN bd = hover ? g.pen_text_sub : g.pen_border;
        ui_round(dis->hDC, dis->rcItem, 8, 8, bg, bd);
        HGDIOBJ old = SelectObject(dis->hDC, self->font_body_bold_);
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, t.text_main);
        DrawTextW(dis->hDC, txt, -1, &dis->rcItem,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dis->hDC, old);
        return TRUE;
    }

    case WM_COMMAND: {
        if (!self) break;
        int id = LOWORD(wParam);
        // 只有按钮的 BN_CLICKED 才响应；过滤编辑框的 EN_* 与列表框通知
        if (HIWORD(wParam) != BN_CLICKED) break;
        switch (id) {
        case IDC_BTN_TOGGLE_BOT:  self->action_toggle_bot(); break;
        case IDC_BTN_SAVE_CONFIG: self->action_save_config(); break;
        case IDC_BTN_TEST_API:    self->action_test_api(); break;
        case IDC_BTN_CLEAR_LOG:
            if (self->list_log_) SendMessageW(self->list_log_, LB_RESETCONTENT, 0, 0);
            self->log_follow_ = true;
            self->log("日志已清空");
            break;
        case IDC_BTN_RELOAD_PLUGINS:
            self->plugin_cache_.clear();
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
        case IDC_BTN_RESET_LAYOUT: {
            for (int k = 0; k < MOD_COUNT; ++k) {
                self->mod_rect_[k] = self->default_module_rect(k);
                self->clamp_module(k);
                self->relayout_module(k);
            }
            for (int k = 0; k < MOD_COUNT; ++k) self->z_order_[k] = k;
            self->apply_z_order();
            self->save_layout();
            if (self->h_wnd_) InvalidateRect(self->h_wnd_, NULL, FALSE);
            self->log("布局已重置");
            break;
        }
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
