#include "ui_window.hpp"
#include "ui_dialogs.hpp"
#include "plugin_manager.hpp"
#include <richedit.h>
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

#define IDC_TAB_AI               1401
#define IDC_TAB_OB               1402
#define IDC_TAB_PLG              1403

// ---------------------------------------------------------------------------
// 设计令牌
// ---------------------------------------------------------------------------
namespace Theme {
    const COLORREF WinBg        = RGB(247, 249, 252);
    const COLORREF HeaderBg     = RGB(255, 255, 255);
    const COLORREF Border       = RGB(226, 232, 240);
    const COLORREF BorderSoft   = RGB(237, 242, 247);
    const COLORREF CardBg       = RGB(255, 255, 255);
    const COLORREF FieldBg      = RGB(244, 247, 250);
    const COLORREF FieldBgFocus = RGB(255, 255, 255);
    const COLORREF TextMain     = RGB(15, 23, 42);
    const COLORREF TextSub      = RGB(100, 116, 139);
    const COLORREF TextFaint    = RGB(148, 163, 184);
    const COLORREF Primary      = RGB(79, 70, 229);
    const COLORREF PrimaryDark  = RGB(67, 56, 202);
    const COLORREF PrimarySoft  = RGB(238, 242, 255);
    const COLORREF Success      = RGB(16, 185, 129);
    const COLORREF SuccessSoft  = RGB(209, 250, 229);
    const COLORREF SuccessText  = RGB(5, 150, 105);
    const COLORREF Warn         = RGB(245, 158, 11);
    const COLORREF Danger       = RGB(239, 68, 68);
    const COLORREF DangerSoft   = RGB(254, 226, 226);
    const COLORREF LogBg        = RGB(11, 18, 32);
    const COLORREF LogText      = RGB(226, 232, 240);
    const COLORREF LogTime      = RGB(100, 116, 139);
    const COLORREF LogInfo      = RGB(203, 213, 225);
    const COLORREF LogOk        = RGB(52, 211, 153);
    const COLORREF LogWarn      = RGB(251, 191, 36);
    const COLORREF LogErr       = RGB(248, 113, 113);
    const COLORREF LogAi        = RGB(147, 197, 253);
}

// ---------------------------------------------------------------------------
// 布局常量（左栏固定宽度，右栏自适应，避免每个控件跟随缩放重排）
// ---------------------------------------------------------------------------
namespace Lay {
    const int HeaderH  = 64;
    const int LeftX    = 24;
    const int LeftW    = 440;
    const int TabY     = 80;
    const int TabH     = 34;
    const int CardY    = 122;
    const int PadX     = 18;                    // 卡片内左右留白
    const int FieldX   = LeftX + PadX;          // 42
    const int FieldW   = LeftW - PadX * 2;      // 404
    const int RightX   = LeftX + LeftW + 16;    // 480
    const int Gap      = 8;
}

// ---------------------------------------------------------------------------
// 基础工具
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

static void fill_round_rect(HDC hdc, const RECT& rc, int radius, COLORREF fill, COLORREF border = CLR_INVALID, int border_w = 1) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, border == CLR_INVALID ? 0 : border_w, border == CLR_INVALID ? fill : border);
    HGDIOBJ ob = SelectObject(hdc, br);
    HGDIOBJ op = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

// 卡片：柔和投影 + 白底 + 1px 描边
static void draw_card(HDC hdc, const RECT& rc, int radius = 12) {
    for (int i = 3; i >= 1; --i) {
        RECT sh = { rc.left - i / 2, rc.top + i, rc.right + i / 2, rc.bottom + i };
        fill_round_rect(hdc, sh, radius + i, RGB(246, 248, 251));
    }
    fill_round_rect(hdc, rc, radius, Theme::CardBg, Theme::Border);
}

// 胶囊标签
static void draw_pill(HDC hdc, const RECT& rc, COLORREF bg, COLORREF fg,
                      const std::wstring& text, HFONT font) {
    fill_round_rect(hdc, rc, rc.bottom - rc.top, bg);
    HGDIOBJ old = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);
    RECT tr = rc;
    DrawTextW(hdc, text.c_str(), -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, old);
}

// 小标题（卡片内）
static void draw_caption(HDC hdc, int x, int y, int w, const wchar_t* text, HFONT font) {
    HGDIOBJ old = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, Theme::TextSub);
    RECT rc = { x, y, x + w, y + 16 };
    DrawTextW(hdc, text, -1, &rc, DT_LEFT | DT_TOP | DT_SINGLELINE);
    SelectObject(hdc, old);
}

static void draw_section_title(HDC hdc, int x, int y, const wchar_t* text, HFONT font) {
    HGDIOBJ old = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, Theme::TextMain);
    RECT rc = { x, y, x + 360, y + 22 };
    DrawTextW(hdc, text, -1, &rc, DT_LEFT | DT_TOP | DT_SINGLELINE);
    SelectObject(hdc, old);
}

// 按钮布局计算（日志工具按钮右对齐，供绘制与摆放共用）
static void compute_tools_layout(int client_w, int& tx0, int& tool_w) {
    const int btn_gap = 8;
    int right_w = client_w - Lay::RightX - 24;
    if (right_w < 320) right_w = 320;
    tool_w = (right_w - btn_gap * 3) / 4;
    if (tool_w < 78) tool_w = 78;
    if (tool_w > 100) tool_w = 100;
    tx0 = Lay::RightX + right_w - (tool_w * 4 + btn_gap * 3);
    if (tx0 < Lay::RightX + 140) tx0 = Lay::RightX + 140;
}

// ---------------------------------------------------------------------------
// 构造 / 资源
// ---------------------------------------------------------------------------
MainWindow::MainWindow() {}

MainWindow::~MainWindow() {
    HFONT fonts[] = { font_brand_, font_h2_, font_body_, font_body_bold_, font_code_, font_log_, font_small_ };
    for (HFONT f : fonts) if (f) DeleteObject(f);

    HBRUSH brushes[] = { brush_win_bg_, brush_card_bg_, brush_input_bg_, brush_field_bg_, brush_log_bg_ };
    for (HBRUSH b : brushes) if (b) DeleteObject(b);
}

MainWindow& MainWindow::get_instance() {
    static MainWindow instance;
    return instance;
}

void MainWindow::init_gdi_resources() {
    // 用负高度 = 字符高度（px），比正高度（单元格高度）更直观，字号也是这次"变好看"的关键
    auto mk = [](int h, int weight, const wchar_t* face) {
        return CreateFontW(-h, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
    };

    font_brand_     = mk(21, FW_BOLD,   L"Microsoft YaHei UI");
    font_h2_        = mk(15, FW_BOLD,   L"Microsoft YaHei UI");
    font_body_      = mk(14, FW_NORMAL, L"Microsoft YaHei UI");
    font_body_bold_ = mk(14, FW_BOLD,   L"Microsoft YaHei UI");
    font_small_     = mk(12, FW_NORMAL, L"Microsoft YaHei UI");
    font_log_       = mk(13, FW_NORMAL, L"Microsoft YaHei UI");
    font_code_      = mk(13, FW_NORMAL, L"Consolas");

    brush_win_bg_   = CreateSolidBrush(Theme::WinBg);
    brush_card_bg_  = CreateSolidBrush(Theme::CardBg);
    brush_input_bg_ = CreateSolidBrush(Theme::FieldBg);
    brush_field_bg_ = CreateSolidBrush(Theme::FieldBg);
    brush_log_bg_   = CreateSolidBrush(Theme::LogBg);
}

LRESULT CALLBACK MainWindow::button_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    MainWindow* self = (MainWindow*)dwRefData;
    switch (uMsg) {
    case WM_ERASEBKGND:
        return 1;
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

    const wchar_t CLASS_NAME[] = L"TabMind_Resizable_Window";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWindow::window_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = brush_win_bg_;
    RegisterClassW(&wc);

    // 默认尺寸按屏幕工作区收敛，避免小屏（如 1366x768）被裁掉底部按钮
    RECT wa = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int win_w = 1180, win_h = 800;
    int max_w = (wa.right - wa.left) - 40;
    int max_h = (wa.bottom - wa.top) - 40;
    if (win_w > max_w) win_w = max_w;
    if (win_h > max_h) win_h = max_h;
    if (win_w < 900) win_w = 900;
    if (win_h < 620) win_h = 620;
    int pos_x = wa.left + ((wa.right - wa.left) - win_w) / 2;
    int pos_y = wa.top + ((wa.bottom - wa.top) - win_h) / 2;

    h_wnd_ = CreateWindowExW(
        0, CLASS_NAME,
        L"TabMind · 智能聊天机器人管理终端",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        pos_x, pos_y, win_w, win_h,
        NULL, NULL, hInstance, this
    );
    if (!h_wnd_) return false;

    config_ = AppConfig::load_from_file("config.json");
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
        log("未配置 API Key，请在「AI 配置」页填写后启动");
    }
    return true;
}

// ---------------------------------------------------------------------------
// 控件创建
// ---------------------------------------------------------------------------
void MainWindow::init_controls(HWND hwnd) {
    auto mk_label = [&](const wchar_t* text, int x, int y, int w, int h, HFONT font) {
        HWND h2 = CreateWindowW(L"STATIC", text, WS_VISIBLE | WS_CHILD | SS_LEFT,
                                x, y, w, h, hwnd, NULL, h_instance_, NULL);
        SendMessageW(h2, WM_SETFONT, (WPARAM)font, TRUE);
        return h2;
    };

    // 扁平输入框：无 3D 边框，浅灰底，内部留 6px 边距
    auto mk_edit = [&](int id, const std::wstring& text, int x, int y, int w, int h, DWORD style) {
        HWND e = CreateWindowExW(0, L"EDIT", text.c_str(),
                                 WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | style,
                                 x, y, w, h, hwnd, (HMENU)(INT_PTR)id, h_instance_, NULL);
        SendMessageW(e, WM_SETFONT, (WPARAM)font_body_, TRUE);
        SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
        return e;
    };

    auto mk_button = [&](int id, const wchar_t* text, int x, int y, int w, int h) {
        HWND b = CreateWindowW(L"BUTTON", text, WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                               x, y, w, h, hwnd, (HMENU)(INT_PTR)id, h_instance_, NULL);
        SetWindowSubclass(b, MainWindow::button_subclass_proc, id, (DWORD_PTR)this);
        return b;
    };

    // ======================= 左侧标签页 =======================
    tab_btn_ai_ = mk_button(IDC_TAB_AI,  L"AI 配置", Lay::LeftX, Lay::TabY, 146, Lay::TabH);
    tab_btn_ob_ = mk_button(IDC_TAB_OB,  L"QQ 接入", Lay::LeftX + 147, Lay::TabY, 146, Lay::TabH);
    tab_btn_plg_ = mk_button(IDC_TAB_PLG, L"插件",   Lay::LeftX + 294, Lay::TabY, 146, Lay::TabH);

    // ---------- Tab 0：AI 配置 ----------
    int fy = Lay::CardY + 46;
    tab0_ctl_.push_back(mk_label(L"API 接口地址 (Base URL)", Lay::FieldX, fy, Lay::FieldW, 16, font_small_));
    edit_base_url_ = mk_edit(IDC_EDIT_BASE_URL, L"", Lay::FieldX, fy + 18, Lay::FieldW, 32, 0);
    tab0_ctl_.push_back(edit_base_url_);

    fy += 62;
    tab0_ctl_.push_back(mk_label(L"API 密钥 (API Key)", Lay::FieldX, fy, Lay::FieldW, 16, font_small_));
    edit_api_key_ = mk_edit(IDC_EDIT_API_KEY, L"", Lay::FieldX, fy + 18, Lay::FieldW, 32, ES_PASSWORD);
    tab0_ctl_.push_back(edit_api_key_);

    fy += 62;
    tab0_ctl_.push_back(mk_label(L"模型名称", Lay::FieldX, fy, 180, 16, font_small_));
    tab0_ctl_.push_back(mk_label(L"Max Tokens", Lay::FieldX + 196, fy, 96, 16, font_small_));
    tab0_ctl_.push_back(mk_label(L"上下文轮数", Lay::FieldX + 300, fy, 104, 16, font_small_));

    combo_model_ = CreateWindowW(L"COMBOBOX", L"",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        Lay::FieldX, fy + 18, 184, 200, hwnd, (HMENU)IDC_COMBO_MODEL, h_instance_, NULL);
    SendMessageW(combo_model_, WM_SETFONT, (WPARAM)font_body_, TRUE);
    SendMessageW(combo_model_, CB_ADDSTRING, 0, (LPARAM)L"deepseek-chat");
    SendMessageW(combo_model_, CB_ADDSTRING, 0, (LPARAM)L"deepseek-reasoner");
    SendMessageW(combo_model_, CB_ADDSTRING, 0, (LPARAM)L"deepseek-v4-flash");
    SendMessageW(combo_model_, CB_ADDSTRING, 0, (LPARAM)L"gpt-4o-mini");
    SendMessageW(combo_model_, CB_ADDSTRING, 0, (LPARAM)L"gpt-4o");
    tab0_ctl_.push_back(combo_model_);

    edit_max_tokens_ = mk_edit(IDC_EDIT_MAX_TOKENS, L"", Lay::FieldX + 196, fy + 18, 96, 32, ES_NUMBER);
    tab0_ctl_.push_back(edit_max_tokens_);
    edit_history_rounds_ = mk_edit(IDC_EDIT_HISTORY_ROUNDS, L"", Lay::FieldX + 300, fy + 18, 104, 32, ES_NUMBER);
    tab0_ctl_.push_back(edit_history_rounds_);

    fy += 62;
    tab0_ctl_.push_back(mk_label(L"系统提示词 / 人设 (System Prompt)", Lay::FieldX, fy, Lay::FieldW, 16, font_small_));
    edit_system_prompt_ = CreateWindowExW(0, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        Lay::FieldX, fy + 18, Lay::FieldW, 120, hwnd, (HMENU)IDC_EDIT_SYSTEM_PROMPT, h_instance_, NULL);
    SendMessageW(edit_system_prompt_, WM_SETFONT, (WPARAM)font_body_, TRUE);
    SendMessageW(edit_system_prompt_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
    tab0_ctl_.push_back(edit_system_prompt_);

    // ---------- Tab 1：QQ 接入 ----------
    fy = Lay::CardY + 46;
    tab1_ctl_.push_back(mk_label(L"OneBot WebSocket 地址", Lay::FieldX, fy, Lay::FieldW, 16, font_small_));
    edit_ws_url_ = mk_edit(IDC_EDIT_WS_URL, L"", Lay::FieldX, fy + 18, Lay::FieldW, 32, 0);
    tab1_ctl_.push_back(edit_ws_url_);

    fy += 62;
    tab1_ctl_.push_back(mk_label(L"机器人 QQ 号", Lay::FieldX, fy, 180, 16, font_small_));
    tab1_ctl_.push_back(mk_label(L"Access Token（无则留空）", Lay::FieldX + 196, fy, 220, 16, font_small_));
    edit_bot_qq_ = mk_edit(IDC_EDIT_BOT_QQ, L"", Lay::FieldX, fy + 18, 184, 32, ES_NUMBER);
    tab1_ctl_.push_back(edit_bot_qq_);
    edit_access_token_ = mk_edit(IDC_EDIT_ACCESS_TOKEN, L"", Lay::FieldX + 196, fy + 18, Lay::FieldW - 196, 32, 0);
    tab1_ctl_.push_back(edit_access_token_);

    fy += 62;
    tab1_ctl_.push_back(mk_label(L"触发前缀词（逗号分隔，如 /chat, /ai, 你好）", Lay::FieldX, fy, Lay::FieldW, 16, font_small_));
    edit_prefixes_ = mk_edit(IDC_EDIT_PREFIXES, L"", Lay::FieldX, fy + 18, Lay::FieldW, 32, 0);
    tab1_ctl_.push_back(edit_prefixes_);

    fy += 62;
    chk_group_require_at_ = CreateWindowW(L"BUTTON", L"群聊必须 @机器人 或命中前缀才响应",
        WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
        Lay::FieldX, fy, Lay::FieldW, 22, hwnd, (HMENU)IDC_CHK_REQUIRE_AT, h_instance_, NULL);
    SendMessageW(chk_group_require_at_, WM_SETFONT, (WPARAM)font_body_, TRUE);
    tab1_ctl_.push_back(chk_group_require_at_);

    fy += 32;
    tab1_ctl_.push_back(mk_label(L"免 @ 插件（DLL 名或插件名关键词，逗号分隔）", Lay::FieldX, fy, Lay::FieldW, 16, font_small_));
    edit_at_exempt_ = mk_edit(IDC_EDIT_AT_EXEMPT, L"", Lay::FieldX, fy + 18, Lay::FieldW, 32, 0);
    tab1_ctl_.push_back(edit_at_exempt_);



    // ---------- Tab 2：插件 ----------
    fy = Lay::CardY + 46;
    label_plugin_summary_ = mk_label(L"正在加载插件...", Lay::FieldX, fy, Lay::FieldW, 44, font_body_);
    tab2_ctl_.push_back(label_plugin_summary_);

    fy += 58;
    btn_reload_plugins_ = mk_button(IDC_BTN_RELOAD_PLUGINS, L"重载插件", Lay::FieldX, fy, 128, 36);
    tab2_ctl_.push_back(btn_reload_plugins_);
    btn_open_plugin_dir_ = mk_button(IDC_BTN_OPEN_PLUGIN_DIR, L"插件目录", Lay::FieldX + 138, fy, 128, 36);
    tab2_ctl_.push_back(btn_open_plugin_dir_);
    btn_plugins_ = mk_button(IDC_BTN_PLUGIN_MGR, L"插件管理", Lay::FieldX + 276, fy, 128, 36);
    tab2_ctl_.push_back(btn_plugins_);



    // ======================= 底部操作区 =======================
    btn_toggle_bot_   = mk_button(IDC_BTN_TOGGLE_BOT, L"启动机器人", 0, 0, 10, 10);
    btn_save_config_  = mk_button(IDC_BTN_SAVE_CONFIG, L"保存配置", 0, 0, 10, 10);
    btn_test_api_     = mk_button(IDC_BTN_TEST_API, L"测试接口", 0, 0, 10, 10);

    // ======================= 右侧日志区 =======================
    btn_advanced_ = mk_button(IDC_BTN_ADVANCED, L"高级设置", 0, 0, 10, 10);
    btn_help_     = mk_button(IDC_BTN_HELP, L"使用说明", 0, 0, 10, 10);
    btn_clear_log_ = mk_button(IDC_BTN_CLEAR_LOG, L"清空", 0, 0, 10, 10);

    // 日志用 RichEdit 以获得彩色行；失败则退回普通 EDIT
    LoadLibraryW(L"Msftedit.dll");
    log_is_riched_ = (GetModuleHandleW(L"Msftedit.dll") != nullptr);
    const wchar_t* log_cls = log_is_riched_ ? L"RICHEDIT50W" : L"EDIT";
    edit_log_ = CreateWindowExW(0, log_cls, L"",
        WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
        WS_VSCROLL | ES_NOHIDESEL,
        0, 0, 10, 10, hwnd, (HMENU)IDC_EDIT_LOG, h_instance_, NULL);
    SendMessageW(edit_log_, EM_LIMITTEXT, 0, 0);
    SendMessageW(edit_log_, WM_SETFONT, (WPARAM)font_log_, TRUE);

    if (log_is_riched_) {
        SendMessageW(edit_log_, EM_SETBKGNDCOLOR, 0, (LPARAM)Theme::LogBg);
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
        cf.crTextColor = Theme::LogText;
        cf.yHeight = 200;   // 10pt（twips）
        lstrcpynW(cf.szFaceName, L"Microsoft YaHei UI", LF_FACESIZE);
        SendMessageW(edit_log_, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    }

    switch_tab(0);
}

// ---------------------------------------------------------------------------
// 标签页切换
// ---------------------------------------------------------------------------
void MainWindow::switch_tab(int index) {
    active_tab_ = index;
    for (size_t i = 0; i < tab0_ctl_.size(); ++i) ShowWindow(tab0_ctl_[i], index == 0 ? SW_SHOW : SW_HIDE);
    for (size_t i = 0; i < tab1_ctl_.size(); ++i) ShowWindow(tab1_ctl_[i], index == 1 ? SW_SHOW : SW_HIDE);
    for (size_t i = 0; i < tab2_ctl_.size(); ++i) ShowWindow(tab2_ctl_[i], index == 2 ? SW_SHOW : SW_HIDE);
    InvalidateRect(tab_btn_ai_, NULL, TRUE);
    InvalidateRect(tab_btn_ob_, NULL, TRUE);
    InvalidateRect(tab_btn_plg_, NULL, TRUE);
    // 同步全量重绘：隐藏掉的上层控件若不清屏会留下文字残影（STATIC 是透明背景，叠字特别明显）
    if (h_wnd_) {
        RedrawWindow(h_wnd_, NULL, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
}

// ---------------------------------------------------------------------------
// 自适应布局
// ---------------------------------------------------------------------------
void MainWindow::relayout_controls(int width, int height) {
    if (width < 700 || height < 500) return;

    // 右侧日志区
    int right_w = width - Lay::RightX - 24;
    if (right_w < 320) right_w = 320;

    // 底部操作区（左栏）
    int btn_y2 = height - 46;
    int btn_y1 = btn_y2 - 52;
    int half = (Lay::LeftW - 10) / 2;

    SetWindowPos(btn_toggle_bot_, NULL, Lay::LeftX, btn_y1, Lay::LeftW, 44, SWP_NOZORDER);
    SetWindowPos(btn_save_config_, NULL, Lay::LeftX, btn_y2, half, 38, SWP_NOZORDER);
    SetWindowPos(btn_test_api_, NULL, Lay::LeftX + half + 10, btn_y2, half, 38, SWP_NOZORDER);

    // 系统提示词框吃掉卡片剩余高度
    int card_bottom = btn_y1 - 14;
    int prompt_h = card_bottom - (Lay::CardY + 46 + 62 * 3 + 18) - 20;
    if (prompt_h < 90) prompt_h = 90;
    SetWindowPos(edit_system_prompt_, NULL, Lay::FieldX, Lay::CardY + 46 + 62 * 3 + 18, Lay::FieldW, prompt_h, SWP_NOZORDER);

    // 日志工具按钮（右对齐）
    int tx0 = 0, tool_w = 88;
    compute_tools_layout(width, tx0, tool_w);
    const int g = 8;
    SetWindowPos(btn_advanced_, NULL, tx0, 80, tool_w, 30, SWP_NOZORDER);
    SetWindowPos(btn_help_, NULL, tx0 + (tool_w + g), 80, tool_w, 30, SWP_NOZORDER);
    SetWindowPos(btn_clear_log_, NULL, tx0 + (tool_w + g) * 2, 80, tool_w, 30, SWP_NOZORDER);

    // 日志主体
    int log_top = 120;
    int log_h = height - log_top - 24;
    if (log_h < 120) log_h = 120;
    int lw = right_w - 4, lh = log_h - 4;
    SetWindowPos(edit_log_, NULL, Lay::RightX + 2, log_top + 2, lw, lh, SWP_NOZORDER);

    // 日志内容留白：RichEdit 用 EM_SETRECT（普通 EDIT 不支持，忽略即可）
    if (log_is_riched_) {
        RECT fr = { 14, 10, lw - 14, lh - 10 };
        SendMessageW(edit_log_, EM_SETRECT, 0, (LPARAM)&fr);
    }

    InvalidateRect(h_wnd_, NULL, TRUE);
}

// ---------------------------------------------------------------------------
// 配置 <-> 界面
// ---------------------------------------------------------------------------
void MainWindow::load_config_to_ui() {
    set_window_text_u8(edit_api_key_, config_.ai.api_key);
    set_window_text_u8(edit_base_url_, config_.ai.base_url);

    // 下拉列表（CBS_DROPDOWNLIST）用 SetWindowText 不会选中，必须按字符串找下标再 CB_SETCURSEL
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

    config_.save_to_file("config.json");
}

// ---------------------------------------------------------------------------
// 状态与插件刷新
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
    std::wstring txt = L"已加载 " + std::to_wstring(list.size()) + L" 个插件，其中 "
                     + std::to_wstring(enabled) + L" 个启用中";
    if (label_plugin_summary_) SetWindowTextW(label_plugin_summary_, txt.c_str());
    if (h_wnd_) InvalidateRect(h_wnd_, NULL, TRUE);
}

// ---------------------------------------------------------------------------
// 行为
// ---------------------------------------------------------------------------
void MainWindow::toggle_bot_state() {
    if (is_bot_running_) {
        if (bot_client_) {
            bot_client_->stop();
            bot_client_.reset();
        }
        is_bot_running_ = false;
        update_status_ui();
        log("机器人已停止");
    } else {
        save_ui_to_config();
        if (config_.ai.api_key.empty()) {
            switch_tab(0);
            SetFocus(edit_api_key_);
            MessageBoxW(h_wnd_, L"请先在「AI 配置」页填写 API Key。", L"缺少配置", MB_OK | MB_ICONWARNING);
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

// RichEdit/EDIT 都没有 emoji 字体回退，emoji 会显示成方块。
// 日志的语义已经用颜色表达，这里直接把 emoji 与变体选择符去掉，避免出现"豆腐块"。
static void strip_emoji(std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        // 代理对（BMP 外的 emoji）
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() &&
            s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            ++i;
            continue;
        }
        if ((c >= 0x2600 && c <= 0x27BF) ||   // 杂项符号 / Dingbats
            (c >= 0x2B00 && c <= 0x2BFF) ||
            c == 0xFE0F || c == 0xFE0E || c == 0x200D ||
            c == 0x2139 || c == 0x2049 || c == 0x203C) {
            continue;
        }
        out.push_back(c);
    }
    // 去掉因删除 emoji 产生的多余空格
    std::wstring cleaned;
    bool prev_space = false;
    for (wchar_t c : out) {
        bool sp = (c == L' ');
        if (sp && prev_space) continue;
        cleaned.push_back(c);
        prev_space = sp;
    }
    // 去掉行首空格
    size_t b = cleaned.find_first_not_of(L' ');
    if (b == std::wstring::npos) cleaned.clear();
    else if (b > 0) cleaned = cleaned.substr(b);
    s.swap(cleaned);
}

// 按内容判定日志颜色
static COLORREF log_color_for(const std::string& raw) {
    auto has = [&](const char* k) { return raw.find(k) != std::string::npos; };
    if (has("❌") || has("失败") || has("错误") || has("异常") || has("FAIL")) return Theme::LogErr;
    if (has("⚠") || has("未配置") || has("警告")) return Theme::LogWarn;
    if (has("✅") || has("√") || has("已上线") || has("已就绪") || has("通过")) return Theme::LogOk;
    if (has("AI") || has("回复") || has("模型")) return Theme::LogAi;
    return Theme::LogInfo;
}

void MainWindow::append_log_text(const std::wstring& text, const std::string& raw_for_scan) {
    HWND log = edit_log_;
    if (!log) return;

    std::wstring shown = text;
    strip_emoji(shown);

    SendMessageW(log, WM_SETREDRAW, FALSE, 0);

    // 裁剪过长的历史
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
    } else {
        int len = GetWindowTextLengthW(log);
        if (len > 120000) {
            SendMessageW(log, EM_SETSEL, 0, 50000);
            SendMessageW(log, EM_REPLACESEL, FALSE, (LPARAM)L"");
            len = GetWindowTextLengthW(log);
        }
        SendMessageW(log, EM_SETSEL, len, len);
    }

    if (log_is_riched_) {
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
        cf.crTextColor = log_color_for(raw_for_scan);
        cf.yHeight = 200;
        lstrcpynW(cf.szFaceName, L"Microsoft YaHei UI", LF_FACESIZE);
        SendMessageW(log, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
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

        std::wstringstream time_prefix;
        time_prefix << L"[" << std::setw(2) << std::setfill(L'0') << tm_now.tm_hour << L":"
                    << std::setw(2) << std::setfill(L'0') << tm_now.tm_min << L":"
                    << std::setw(2) << std::setfill(L'0') << tm_now.tm_sec << L"]  ";

        SendMessageW(self->edit_log_, WM_SETREDRAW, FALSE, 0);
        for (const auto& m : batch) {
            self->append_log_text(time_prefix.str() + utf8_to_wstring(m) + L"\r\n", m);
            if (m.find("[BotClient] -> 回复成功") != std::string::npos) {
                self->total_msg_handled_++;
            }
        }
        SendMessageW(self->edit_log_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(self->edit_log_, NULL, TRUE);
        self->update_status_ui();
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
            self->relayout_controls(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 940;
        mmi->ptMinTrackSize.y = 620;
        return 0;
    }

    case WM_ERASEBKGND: {
        HDC hdcErase = (HDC)wParam;
        RECT rcErase;
        GetClientRect(hwnd, &rcErase);
        FillRect(hdcErase, &rcErase, self ? self->brush_win_bg_ : (HBRUSH)GetStockObject(WHITE_BRUSH));
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (!self) { EndPaint(hwnd, &ps); return 0; }

        // 背景
        FillRect(hdc, &rc, self->brush_win_bg_);

        // ---- 顶栏 ----
        RECT rcHeader = { 0, 0, rc.right, Lay::HeaderH };
        FillRect(hdc, &rcHeader, self->brush_card_bg_);
        HPEN pen = CreatePen(PS_SOLID, 1, Theme::BorderSoft);
        HGDIOBJ op = SelectObject(hdc, pen);
        MoveToEx(hdc, 0, Lay::HeaderH, NULL);
        LineTo(hdc, rc.right, Lay::HeaderH);
        SelectObject(hdc, op);
        DeleteObject(pen);

        // 品牌方块
        RECT rcLogo = { Lay::LeftX, 14, Lay::LeftX + 36, 50 };
        fill_round_rect(hdc, rcLogo, 10, Theme::Primary);
        {
            HGDIOBJ old = SelectObject(hdc, self->font_brand_);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            RECT t = rcLogo;
            DrawTextW(hdc, L"T", -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, old);
        }

        draw_section_title(hdc, Lay::LeftX + 48, 16, L"TabMind", self->font_brand_);
        draw_caption(hdc, Lay::LeftX + 50, 40, 320, L"AI Agent Console · OneBot v11", self->font_small_);

        // 右侧状态胶囊
        {
            int right_edge = rc.right - 24;
            RECT rcCount = { right_edge - 118, 18, right_edge, 46 };
            std::wstring cnt = L"已回复 " + std::to_wstring(self->total_msg_handled_);
            draw_pill(hdc, rcCount, Theme::FieldBg, Theme::TextMain, cnt, self->font_small_);

            RECT rcModel = { rcCount.left - 172, 18, rcCount.left - 8, 46 };
            std::wstring model = L"模型 " + utf8_to_wstring(self->config_.ai.model);
            draw_pill(hdc, rcModel, Theme::PrimarySoft, Theme::Primary, model, self->font_small_);

            RECT rcStatus = { rcModel.left - 152, 18, rcModel.left - 8, 46 };
            bool run = self->is_bot_running_;
            draw_pill(hdc, rcStatus,
                      run ? Theme::SuccessSoft : Theme::DangerSoft,
                      run ? Theme::SuccessText : Theme::Danger,
                      run ? L"● 运行中" : L"● 已停止", self->font_small_);
        }

        // ---- 左侧：标签页底槽 ----
        RECT rcTabBar = { Lay::LeftX, Lay::TabY, Lay::LeftX + Lay::LeftW, Lay::TabY + Lay::TabH };
        fill_round_rect(hdc, rcTabBar, 10, RGB(238, 242, 247));

        // ---- 左侧：卡片 ----
        int btn_y2 = rc.bottom - 46;
        int btn_y1 = btn_y2 - 52;
        int card_bottom = btn_y1 - 14;
        RECT rcCard = { Lay::LeftX, Lay::CardY, Lay::LeftX + Lay::LeftW, card_bottom };
        draw_card(hdc, rcCard, 12);

        // 当前标签页标题
        const wchar_t* tab_titles[] = { L"AI 大模型接口", L"OneBot / QQ 接入", L"扩展插件" };
        draw_section_title(hdc, Lay::FieldX, Lay::CardY + 16, tab_titles[self->active_tab_], self->font_h2_);

        // ---- 插件页：实时插件清单（填满中间空白，也方便一眼看到装了什么）----
        if (self->active_tab_ == 2) {
            auto plugins = PluginManager::get_instance().get_plugin_list();
            int list_top = Lay::CardY + 178;
            int list_bottom = card_bottom - 96;
            int row_h = 30;
            int max_rows = (list_bottom - list_top) / row_h;
            if (max_rows < 1) max_rows = 1;

            HGDIOBJ old = SelectObject(hdc, self->font_small_);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, Theme::TextFaint);
            RECT rcHead = { Lay::FieldX, list_top - 22, Lay::FieldX + Lay::FieldW, list_top - 4 };
            DrawTextW(hdc, L"当前已加载", -1, &rcHead, DT_LEFT | DT_TOP | DT_SINGLELINE);

            if (plugins.empty()) {
                SetTextColor(hdc, Theme::TextFaint);
                RECT rcEmpty = { Lay::FieldX, list_top + 4, Lay::FieldX + Lay::FieldW, list_top + 26 };
                DrawTextW(hdc, L"暂无插件 · 把 DLL 放进 plugins 目录后点「重载插件」",
                          -1, &rcEmpty, DT_LEFT | DT_TOP | DT_SINGLELINE);
            } else {
                int shown = 0;
                for (const auto& pl : plugins) {
                    if (shown >= max_rows) break;
                    int y = list_top + shown * row_h;

                    RECT rcRow = { Lay::FieldX, y, Lay::FieldX + Lay::FieldW, y + row_h - 4 };
                    fill_round_rect(hdc, rcRow, 7, RGB(248, 250, 252));

                    HBRUSH dot = CreateSolidBrush(pl.enabled ? Theme::Success : Theme::TextFaint);
                    HGDIOBJ ob = SelectObject(hdc, dot);
                    HGDIOBJ opn = SelectObject(hdc, GetStockObject(NULL_PEN));
                    Ellipse(hdc, Lay::FieldX + 10, y + 8, Lay::FieldX + 18, y + 16);
                    SelectObject(hdc, ob);
                    SelectObject(hdc, opn);
                    DeleteObject(dot);

                    SelectObject(hdc, self->font_body_);
                    SetTextColor(hdc, pl.enabled ? Theme::TextMain : Theme::TextSub);
                    RECT rcName = { Lay::FieldX + 26, y + 4, Lay::FieldX + 250, y + row_h - 4 };
                    DrawTextW(hdc, utf8_to_wstring(pl.name).c_str(), -1, &rcName,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                    SelectObject(hdc, self->font_small_);
                    SetTextColor(hdc, Theme::TextFaint);
                    RECT rcVer = { Lay::FieldX + Lay::FieldW - 110, y + 4, Lay::FieldX + Lay::FieldW - 12, y + row_h - 4 };
                    DrawTextW(hdc, utf8_to_wstring("v" + pl.version).c_str(), -1, &rcVer,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                    ++shown;
                }
                if ((int)plugins.size() > shown) {
                    RECT rcMore = { Lay::FieldX, list_top + shown * row_h, Lay::FieldX + Lay::FieldW, list_top + shown * row_h + 20 };
                    SetTextColor(hdc, Theme::TextFaint);
                    std::wstring more = L"还有 " + std::to_wstring(plugins.size() - shown) + L" 个，点「插件管理」查看全部";
                    DrawTextW(hdc, more.c_str(), -1, &rcMore, DT_LEFT | DT_TOP | DT_SINGLELINE);
                }
            }
            SelectObject(hdc, old);
        }

        // ---- 标签页底部信息面板（填满留白，同时给出关键提示）----
        {
            int info_bottom = card_bottom - 18;
            int info_h = (self->active_tab_ == 1) ? 92 : 78;
            int info_top = info_bottom - info_h;
            if (self->active_tab_ != 0 && info_top > Lay::CardY + 120) {
                RECT rcInfo = { Lay::FieldX, info_top, Lay::FieldX + Lay::FieldW, info_bottom };
                fill_round_rect(hdc, rcInfo, 10, Theme::PrimarySoft);
                RECT rcBar = { rcInfo.left, rcInfo.top + 12, rcInfo.left + 3, rcInfo.bottom - 12 };
                fill_round_rect(hdc, rcBar, 2, Theme::Primary);

                const wchar_t* title = (self->active_tab_ == 1) ? L"群聊回复规则" : L"插件说明";
                const wchar_t* body =
                    (self->active_tab_ == 1)
                        ? L"默认需要 @机器人 或命中触发前缀才会回复。\n"
                          L"把插件关键词填进「免 @ 插件」后，这些插件在群里\n"
                          L"不 @ 也能响应（适合随机图、随机老婆这类玩法）。"
                        : L"把 DLL 放进 plugins 目录后点「重载插件」生效。\n"
                          L"插件返回 true 拦截消息、不消耗 Token；false 交给 AI。";

                RECT rcT = { rcInfo.left + 16, rcInfo.top + 12, rcInfo.right - 12, rcInfo.top + 32 };
                HGDIOBJ old = SelectObject(hdc, self->font_body_bold_);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, Theme::PrimaryDark);
                DrawTextW(hdc, title, -1, &rcT, DT_LEFT | DT_TOP | DT_SINGLELINE);

                RECT rcB = { rcInfo.left + 16, rcInfo.top + 36, rcInfo.right - 12, rcInfo.bottom - 10 };
                SelectObject(hdc, self->font_small_);
                SetTextColor(hdc, RGB(71, 85, 105));
                DrawTextW(hdc, body, -1, &rcB, DT_LEFT | DT_TOP | DT_NOCLIP);
                SelectObject(hdc, old);
            }
        }

        // ---- 右侧：日志卡片 ----
        int right_x = Lay::RightX;
        int right_w = rc.right - right_x - 24;
        if (right_w < 320) right_w = 320;
        int log_top = 120;
        int log_h = rc.bottom - log_top - 24;
        if (log_h < 120) log_h = 120;

        RECT rcLogCard = { right_x, 74, right_x + right_w, rc.bottom - 16 };
        draw_card(hdc, rcLogCard, 12);

        // 日志内圈（深色底 + 圆角），控件本身也是深色，视觉上连成一体
        RECT rcLogInner = { right_x + 2, log_top, right_x + right_w - 2, log_top + log_h };
        fill_round_rect(hdc, rcLogInner, 10, Theme::LogBg);

        draw_section_title(hdc, right_x + 18, 88, L"运行日志", self->font_h2_);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (!self) break;
        const bool hover = self->hovered_.count((int)dis->CtlID) > 0;
        const bool pressed = (dis->itemState & ODS_SELECTED) != 0;

        auto text_of_btn = [&](wchar_t* buf, int n) { GetWindowTextW(dis->hwndItem, buf, n); };

        // 主操作按钮（启动 / 停止）
        if (dis->CtlID == IDC_BTN_TOGGLE_BOT) {
            COLORREF bg = self->is_bot_running_ ? Theme::Danger : Theme::Primary;
            if (hover) bg = self->is_bot_running_ ? RGB(220, 38, 38) : Theme::PrimaryDark;
            if (pressed) bg = self->is_bot_running_ ? RGB(185, 28, 28) : RGB(55, 48, 163);
            fill_round_rect(dis->hDC, dis->rcItem, 10, bg);

            HGDIOBJ old = SelectObject(dis->hDC, self->font_h2_);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, RGB(255, 255, 255));
            wchar_t txt[64] = {};
            text_of_btn(txt, 64);
            DrawTextW(dis->hDC, txt, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dis->hDC, old);
            return TRUE;
        }

        // 标签页
        if (dis->CtlID == IDC_TAB_AI || dis->CtlID == IDC_TAB_OB || dis->CtlID == IDC_TAB_PLG) {
            bool active = (dis->CtlID == IDC_TAB_AI && self->active_tab_ == 0) ||
                          (dis->CtlID == IDC_TAB_OB && self->active_tab_ == 1) ||
                          (dis->CtlID == IDC_TAB_PLG && self->active_tab_ == 2);
            RECT r = dis->rcItem;
            if (active) {
                RECT shadow = { r.left + 1, r.top + 1, r.right - 1, r.bottom + 1 };
                fill_round_rect(dis->hDC, shadow, 9, RGB(226, 232, 240));
                RECT inner = { r.left + 1, r.top + 1, r.right - 1, r.bottom - 1 };
                fill_round_rect(dis->hDC, inner, 9, Theme::CardBg);
            } else if (hover) {
                RECT inner = { r.left + 2, r.top + 2, r.right - 2, r.bottom - 2 };
                fill_round_rect(dis->hDC, inner, 8, RGB(248, 250, 252));
            }
            HGDIOBJ old = SelectObject(dis->hDC, active ? self->font_body_bold_ : self->font_body_);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, active ? Theme::Primary : Theme::TextSub);
            wchar_t txt[64] = {};
            text_of_btn(txt, 64);
            RECT tr = dis->rcItem;
            DrawTextW(dis->hDC, txt, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dis->hDC, old);
            return TRUE;
        }

        // 日志工具按钮（幽灵按钮）
        if (dis->CtlID == IDC_BTN_ADVANCED || dis->CtlID == IDC_BTN_HELP || dis->CtlID == IDC_BTN_CLEAR_LOG) {
            COLORREF bg = pressed ? RGB(226, 232, 240) : (hover ? RGB(241, 245, 249) : Theme::CardBg);
            fill_round_rect(dis->hDC, dis->rcItem, 8, bg, Theme::Border);
            HGDIOBJ old = SelectObject(dis->hDC, self->font_small_);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, dis->CtlID == IDC_BTN_CLEAR_LOG ? Theme::TextSub : Theme::Primary);
            wchar_t txt[64] = {};
            text_of_btn(txt, 64);
            DrawTextW(dis->hDC, txt, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dis->hDC, old);
            return TRUE;
        }

        // 次级按钮（白底描边）
        if (dis->CtlID == IDC_BTN_SAVE_CONFIG || dis->CtlID == IDC_BTN_TEST_API ||
            dis->CtlID == IDC_BTN_RELOAD_PLUGINS || dis->CtlID == IDC_BTN_OPEN_PLUGIN_DIR ||
            dis->CtlID == IDC_BTN_PLUGIN_MGR) {
            COLORREF bg = pressed ? RGB(226, 232, 240) : (hover ? RGB(248, 250, 252) : Theme::CardBg);
            COLORREF border = hover ? RGB(203, 213, 225) : Theme::Border;
            fill_round_rect(dis->hDC, dis->rcItem, 9, bg, border);
            HGDIOBJ old = SelectObject(dis->hDC, self->font_body_bold_);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, Theme::TextMain);
            wchar_t txt[64] = {};
            text_of_btn(txt, 64);
            DrawTextW(dis->hDC, txt, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dis->hDC, old);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        // 卡片内标签统一白底（与卡片同色），否则 STATIC 会用系统按钮灰，出现难看的灰条
        SetBkColor(hdc, Theme::CardBg);
        SetBkMode(hdc, OPAQUE);
        SetTextColor(hdc, Theme::TextSub);
        return (LRESULT)(self ? self->brush_card_bg_ : (HBRUSH)GetStockObject(WHITE_BRUSH));
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        HWND ctl = (HWND)lParam;
        if (self && ctl == self->edit_log_) {
            SetBkColor(hdc, Theme::LogBg);
            SetTextColor(hdc, Theme::LogText);
            return (LRESULT)self->brush_log_bg_;
        }
        SetBkColor(hdc, Theme::FieldBg);
        SetTextColor(hdc, Theme::TextMain);
        return (LRESULT)self->brush_field_bg_;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        if (!self) break;
        switch (wmId) {
        case IDC_TAB_AI:  self->switch_tab(0); break;
        case IDC_TAB_OB:  self->switch_tab(1); break;
        case IDC_TAB_PLG: self->switch_tab(2); break;

        case IDC_BTN_TOGGLE_BOT:
            self->toggle_bot_state();
            break;
        case IDC_BTN_SAVE_CONFIG:
            self->save_ui_to_config();
            self->log("配置已保存到 config.json");
            MessageBoxW(hwnd, L"配置已保存。", L"保存成功", MB_OK | MB_ICONINFORMATION);
            break;
        case IDC_BTN_TEST_API:
            self->test_api_connection();
            break;
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
