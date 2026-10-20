#include "ui_dialogs.hpp"
#include "ui_theme.hpp"
#include "plugin_manager.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <shellapi.h>

#pragma comment(lib, "comctl32.lib")

// ---------------------------------------------------------------------------
// 通用工具
// ---------------------------------------------------------------------------
namespace {

// 颜色全部跟随主界面主题（ui_theme.hpp）
#define kWinBg     (theme().win_bg)
#define kCardBg    (theme().card_bg)
#define kBorder    (theme().border)
#define kTextMain  (theme().text_main)
#define kTextMuted (theme().text_sub)
#define kPrimary   (theme().primary)
#define kSuccess   (theme().success)
#define kDanger    (theme().danger)

std::wstring u8w(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string w8u(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::string text_of(HWND h) {
    if (!h) return "";
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return "";
    std::vector<wchar_t> buf(len + 1, 0);
    GetWindowTextW(h, buf.data(), len + 1);
    return w8u(std::wstring(buf.data()));
}

void set_text(HWND h, const std::string& s) {
    if (h) SetWindowTextW(h, u8w(s).c_str());
}

int to_int(const std::string& s, int def) {
    try { return std::stoi(s); } catch (...) { return def; }
}

float to_float(const std::string& s, float def) {
    try { return std::stof(s); } catch (...) { return def; }
}

// 分/合逗号分隔的列表
std::vector<std::string> split_list(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        auto b = item.find_first_not_of(" \t\r\n");
        auto e = item.find_last_not_of(" \t\r\n");
        if (b != std::string::npos) out.push_back(item.substr(b, e - b + 1));
    }
    return out;
}

std::string join_list(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        s += v[i];
        if (i + 1 < v.size()) s += ", ";
    }
    return s;
}

// 分类注册窗口类（重复注册忽略）
void ensure_class(const wchar_t* name, WNDPROC proc, HINSTANCE inst, UINT style = CS_DBLCLKS) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.lpszClassName = name;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kWinBg);
    wc.style = style;
    RegisterClassW(&wc);   // 已存在时返回 0，忽略即可
}

// 把窗口居中到父窗口
void center_on(HWND dlg, HWND parent, int w, int h) {
    RECT pr{};
    if (parent && GetWindowRect(parent, &pr)) {
        int x = pr.left + ((pr.right - pr.left) - w) / 2;
        int y = pr.top + ((pr.bottom - pr.top) - h) / 2;
        SetWindowPos(dlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
}

// 模态循环：禁用父窗口，直到 dlg 被销毁
void run_modal(HWND parent, HWND dlg) {
    EnableWindow(parent, FALSE);
    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
}

// 创建一组常用控件
HWND mk_label(HWND parent, const wchar_t* text, int x, int y, int w, int h, HFONT font = nullptr) {
    HWND label = CreateWindowW(L"STATIC", text, WS_VISIBLE | WS_CHILD,
                               x, y, w, h, parent, nullptr, nullptr, nullptr);
    if (font) SendMessageW(label, WM_SETFONT, (WPARAM)font, TRUE);
    return label;
}

HWND mk_edit(HWND parent, int id, const std::wstring& text, int x, int y, int w, int h,
             DWORD style = 0, HFONT font = nullptr) {
    HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text.c_str(),
                             WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | style,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    if (font) SendMessageW(e, WM_SETFONT, (WPARAM)font, TRUE);
    return e;
}

HWND mk_check(HWND parent, int id, const wchar_t* text, bool checked, int x, int y, int w, int h) {
    HWND c = CreateWindowW(L"BUTTON", text, WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                           x, y, w, h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(c, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return c;
}

HFONT mk_font(int size, int weight, const wchar_t* face, bool fixed_pitch = false) {
    // 负高度 = 字符高度（px）；正高度是单元格高度，会让字显得偏小
    return CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY,
                       fixed_pitch ? (FIXED_PITCH | FF_MODERN) : (DEFAULT_PITCH | FF_SWISS),
                       face);
}

// 圆角按钮（自绘）
void draw_button(const DRAWITEMSTRUCT* dis, COLORREF bg, COLORREF fg, COLORREF border,
                 HFONT font, int radius = 6) {
    HBRUSH br = CreateSolidBrush(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, border == CLR_INVALID ? bg : border);
    HGDIOBJ obr = SelectObject(dis->hDC, br);
    HGDIOBJ open = SelectObject(dis->hDC, pen);
    RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right,
              dis->rcItem.bottom, radius, radius);
    SelectObject(dis->hDC, obr);
    SelectObject(dis->hDC, open);
    DeleteObject(br);
    DeleteObject(pen);

    wchar_t txt[128] = {};
    GetWindowTextW(dis->hwndItem, txt, 128);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, fg);
    if (font) SelectObject(dis->hDC, font);
    RECT rc = dis->rcItem;
    DrawTextW(dis->hDC, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ===========================================================================
// 高级设置
// ===========================================================================
const wchar_t* kAdvClass = L"TabMind_AdvSettings";
const int IDC_ADV_TEMP      = 2001;
const int IDC_ADV_TOKENS    = 2002;
const int IDC_ADV_ROUNDS    = 2003;
const int IDC_ADV_COOLDOWN  = 2004;
const int IDC_ADV_MEM_CHARS = 2005;
const int IDC_ADV_MEM_BATCH = 2006;
const int IDC_ADV_VISION    = 2007;
const int IDC_ADV_AUTH      = 2008;
const int IDC_ADV_AUTODEL   = 2009;
const int IDC_ADV_THINK     = 2010;
const int IDC_ADV_SMART     = 2011;
const int IDC_ADV_FRIENDS   = 2012;
const int IDC_ADV_SAVE      = 2020;
const int IDC_ADV_CANCEL    = 2021;

struct AdvCtx {
    AppConfig* cfg = nullptr;
    bool saved = false;
    HFONT f_body = nullptr;
    HFONT f_small = nullptr;
    HFONT f_section = nullptr;
    HFONT f_bold = nullptr;
};

LRESULT CALLBACK adv_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AdvCtx* ctx = (AdvCtx*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        ctx = (AdvCtx*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);

        ctx->f_body    = mk_font(13, FW_NORMAL, L"Microsoft YaHei UI");
        ctx->f_small   = mk_font(12, FW_NORMAL, L"Microsoft YaHei UI");
        ctx->f_section = mk_font(14, FW_BOLD,   L"Microsoft YaHei UI");
        ctx->f_bold    = mk_font(14, FW_BOLD,   L"Microsoft YaHei UI");

        const AIConfig& ai = ctx->cfg->ai;
        const OneBotConfig& ob = ctx->cfg->onebot;

        int lbl_x = 24, ed_x = 150, ed_w = 90, row1 = 44, row2 = 84;

        mk_label(hwnd, L"🕹  采样与长度", 24, 14, 300, 22, ctx->f_section);

        mk_label(hwnd, L"温度 (0~2)", 24, row1, 110, 20, ctx->f_small);
        mk_edit(hwnd, IDC_ADV_TEMP, u8w(std::to_string(ai.temperature).substr(0, 4)),
                ed_x, row1 - 4, ed_w, 26, 0, ctx->f_body);

        mk_label(hwnd, L"Max Tokens", 268, row1, 100, 20, ctx->f_small);
        mk_edit(hwnd, IDC_ADV_TOKENS, std::to_wstring(ai.max_tokens), 372, row1 - 4, ed_w, 26, ES_NUMBER, ctx->f_body);

        mk_label(hwnd, L"上下文轮数", 24, row2, 120, 20, ctx->f_small);
        mk_edit(hwnd, IDC_ADV_ROUNDS, std::to_wstring(ai.max_history_rounds), ed_x, row2 - 4, ed_w, 26, ES_NUMBER, ctx->f_body);

        mk_label(hwnd, L"接话冷却 (秒)", 268, row2, 100, 20, ctx->f_small);
        mk_edit(hwnd, IDC_ADV_COOLDOWN, std::to_wstring(ai.reply_cooldown_sec), 372, row2 - 4, ed_w, 26, ES_NUMBER, ctx->f_body);

        mk_label(hwnd, L"🧠  长期记忆", 24, 122, 300, 22, ctx->f_section);

        mk_label(hwnd, L"记忆上限 (字)", 24, 156, 120, 20, ctx->f_small);
        mk_edit(hwnd, IDC_ADV_MEM_CHARS, std::to_wstring(ai.memory_max_chars), ed_x, 152, ed_w, 26, ES_NUMBER, ctx->f_body);

        mk_label(hwnd, L"简写阈值 (条)", 268, 156, 100, 20, ctx->f_small);
        mk_edit(hwnd, IDC_ADV_MEM_BATCH, std::to_wstring(ai.memory_batch_size), 372, 152, ed_w, 26, ES_NUMBER, ctx->f_body);

        mk_label(hwnd, L"👁  视觉与鉴权", 24, 194, 300, 22, ctx->f_section);

        mk_label(hwnd, L"视觉模型 (留空则沿用主模型)", 24, 228, 300, 20, ctx->f_small);
        mk_edit(hwnd, IDC_ADV_VISION, u8w(ai.vision_model), 24, 248, 520, 26, 0, ctx->f_body);

        mk_label(hwnd, L"鉴权头方式", 24, 288, 120, 20, ctx->f_small);
        HWND combo = CreateWindowW(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                                   128, 284, 260, 180, hwnd, (HMENU)(INT_PTR)IDC_ADV_AUTH, nullptr, nullptr);
        SendMessageW(combo, WM_SETFONT, (WPARAM)ctx->f_body, TRUE);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"both（两种都发，兼容性最好）");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"bearer（Authorization: Bearer）");
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"api-key（api-key 头）");
        int sel = 0;
        if (ai.auth_style == "bearer") sel = 1;
        else if (ai.auth_style == "api-key") sel = 2;
        SendMessageW(combo, CB_SETCURSEL, sel, 0);

        mk_label(hwnd, L"⚙  行为开关", 24, 326, 300, 22, ctx->f_section);
        mk_check(hwnd, IDC_ADV_AUTODEL, L"自动删除已处理过的本地图片（省磁盘）", ai.auto_delete_images, 24, 352, 520, 24);
        mk_check(hwnd, IDC_ADV_THINK,   L"关闭思考模式（防止 reasoning 占用 max_tokens 导致空回复）", !ai.enable_thinking ? true : false, 24, 380, 520, 24);
        mk_check(hwnd, IDC_ADV_SMART,   L"宽松接话：群内未 @ 也允许模型自行判断要不要说话", ob.smart_reply, 24, 408, 520, 24);

        mk_label(hwnd, L"亲近好友 QQ（逗号分隔，这些人说话可主动接）", 24, 442, 420, 20, ctx->f_small);
        mk_edit(hwnd, IDC_ADV_FRIENDS, u8w(join_list(ai.close_friends)), 24, 462, 520, 26, 0, ctx->f_body);

        HWND ok = CreateWindowW(L"BUTTON", L"💾 保存", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                                296, 512, 118, 34, hwnd, (HMENU)(INT_PTR)IDC_ADV_SAVE, nullptr, nullptr);
        HWND cancel = CreateWindowW(L"BUTTON", L"取消", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                                    426, 512, 118, 34, hwnd, (HMENU)(INT_PTR)IDC_ADV_CANCEL, nullptr, nullptr);
        (void)ok; (void)cancel;

        // 给静态文本统一字体
        EnumChildWindows(hwnd, [](HWND child, LPARAM param) -> BOOL {
            AdvCtx* c = (AdvCtx*)param;
            wchar_t cls[32] = {};
            GetClassNameW(child, cls, 32);
            if (lstrcmpW(cls, L"STATIC") == 0) {
                SendMessageW(child, WM_SETFONT, (WPARAM)c->f_small, TRUE);
            } else if (lstrcmpW(cls, L"BUTTON") == 0) {
                SendMessageW(child, WM_SETFONT, (WPARAM)c->f_bold, TRUE);
            }
            return TRUE;
        }, (LPARAM)ctx);
        return 0;
    }

    case WM_ERASEBKGND: {
        HDC hdcE = (HDC)wp;
        RECT rcE;
        GetClientRect(hwnd, &rcE);
        HBRUSH br = CreateSolidBrush(kWinBg);
        FillRect(hdcE, &rcE, br);
        DeleteObject(br);
        return 1;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
        if (!ctx) break;
        if (dis->CtlID == IDC_ADV_SAVE) {
            COLORREF bg = (dis->itemState & ODS_SELECTED) ? RGB(67, 56, 202) : kPrimary;
            draw_button(dis, bg, RGB(255, 255, 255), CLR_INVALID, ctx->f_bold, 8);
            return TRUE;
        }
        if (dis->CtlID == IDC_ADV_CANCEL) {
            COLORREF bg = (dis->itemState & ODS_SELECTED) ? RGB(226, 232, 240) : kCardBg;
            draw_button(dis, bg, kTextMain, kBorder, ctx->f_bold, 8);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, kCardBg);
        SetBkMode(hdc, OPAQUE);
        SetTextColor(hdc, kTextMain);
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_ADV_CANCEL) { DestroyWindow(hwnd); return 0; }
        if (id == IDC_ADV_SAVE) {
            if (!ctx || !ctx->cfg) { DestroyWindow(hwnd); return 0; }
            AIConfig& ai = ctx->cfg->ai;
            ai.temperature        = to_float(text_of(GetDlgItem(hwnd, IDC_ADV_TEMP)), ai.temperature);
            if (ai.temperature < 0.0f) ai.temperature = 0.0f;
            if (ai.temperature > 2.0f) ai.temperature = 2.0f;
            ai.max_tokens         = to_int(text_of(GetDlgItem(hwnd, IDC_ADV_TOKENS)), ai.max_tokens);
            ai.max_history_rounds = to_int(text_of(GetDlgItem(hwnd, IDC_ADV_ROUNDS)), ai.max_history_rounds);
            ai.reply_cooldown_sec = to_int(text_of(GetDlgItem(hwnd, IDC_ADV_COOLDOWN)), ai.reply_cooldown_sec);
            ai.memory_max_chars   = to_int(text_of(GetDlgItem(hwnd, IDC_ADV_MEM_CHARS)), ai.memory_max_chars);
            ai.memory_batch_size  = to_int(text_of(GetDlgItem(hwnd, IDC_ADV_MEM_BATCH)), ai.memory_batch_size);
            ai.vision_model       = text_of(GetDlgItem(hwnd, IDC_ADV_VISION));

            int auth = (int)SendDlgItemMessageW(hwnd, IDC_ADV_AUTH, CB_GETCURSEL, 0, 0);
            ai.auth_style = (auth == 1) ? "bearer" : (auth == 2 ? "api-key" : "both");

            ai.auto_delete_images = (SendDlgItemMessageW(hwnd, IDC_ADV_AUTODEL, BM_GETCHECK, 0, 0) == BST_CHECKED);
            ai.enable_thinking    = (SendDlgItemMessageW(hwnd, IDC_ADV_THINK, BM_GETCHECK, 0, 0) != BST_CHECKED);
            ctx->cfg->onebot.smart_reply = (SendDlgItemMessageW(hwnd, IDC_ADV_SMART, BM_GETCHECK, 0, 0) == BST_CHECKED);
            ai.close_friends      = split_list(text_of(GetDlgItem(hwnd, IDC_ADV_FRIENDS)));

            ctx->saved = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (ctx) {
            if (ctx->f_body) DeleteObject(ctx->f_body);
            if (ctx->f_small) DeleteObject(ctx->f_small);
            if (ctx->f_section) DeleteObject(ctx->f_section);
            if (ctx->f_bold) DeleteObject(ctx->f_bold);
            ctx->f_body = ctx->f_small = ctx->f_section = ctx->f_bold = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ===========================================================================
// 插件管理
// ===========================================================================
const wchar_t* kPluginClass = L"TabMind_PluginMgr";
const int IDC_PLG_LIST    = 2101;
const int IDC_PLG_RELOAD  = 2102;
const int IDC_PLG_OPENDIR = 2103;
const int IDC_PLG_TOGGLE  = 2104;
const int IDC_PLG_CLOSE   = 2105;

struct PlgCtx {
    HFONT f_body = nullptr;
    HFONT f_bold = nullptr;
    std::vector<PluginInfo> items;
};

void plg_fill(HWND hwnd, PlgCtx* ctx) {
    HWND lv = GetDlgItem(hwnd, IDC_PLG_LIST);
    if (!lv) return;
    SendMessageW(lv, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(lv);
    ctx->items = PluginManager::get_instance().get_plugin_list();
    for (size_t i = 0; i < ctx->items.size(); ++i) {
        const PluginInfo& p = ctx->items[i];
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = (int)i;
        std::wstring state = p.enabled ? L"✅ 启用" : L"⛔ 停用";
        it.pszText = (LPWSTR)state.c_str();
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
        auto set_col = [&](int col, const std::wstring& text) {
            ListView_SetItemText(lv, (int)i, col, (LPWSTR)text.c_str());
        };
        set_col(1, u8w(p.name));
        set_col(2, u8w(p.version));
        set_col(3, u8w(p.description));
    }
    SendMessageW(lv, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lv, nullptr, TRUE);
}

LRESULT CALLBACK plg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PlgCtx* ctx = (PlgCtx*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        ctx = (PlgCtx*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);
        ctx->f_body = mk_font(13, FW_NORMAL, L"Microsoft YaHei UI");
        ctx->f_bold = mk_font(15, FW_BOLD, L"Microsoft YaHei UI");

        HWND lv = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                  WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                  16, 16, 560, 396, hwnd, (HMENU)(INT_PTR)IDC_PLG_LIST, nullptr, nullptr);
        SendMessageW(lv, WM_SETFONT, (WPARAM)ctx->f_body, TRUE);
        ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        struct ColDef { const wchar_t* text; int width; };
        const ColDef cols[] = { { L"状态", 76 }, { L"名称", 170 }, { L"版本", 76 }, { L"说明", 226 } };
        for (int i = 0; i < 4; ++i) {
            LVCOLUMNW col = {};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
            col.fmt = LVCFMT_LEFT;
            col.pszText = (LPWSTR)cols[i].text;
            col.cx = cols[i].width;
            col.iSubItem = i;
            SendMessageW(lv, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
        }

        // 底部说明
        HWND hint = CreateWindowW(L"STATIC", L"选中一行后点「启用 / 停用」可单独开关该插件",
                                  WS_VISIBLE | WS_CHILD, 16, 484, 420, 18, hwnd, NULL, nullptr, nullptr);
        SendMessageW(hint, WM_SETFONT, (WPARAM)ctx->f_body, TRUE);

        const wchar_t* labels[] = {L"🔄 重载插件", L"📂 插件目录", L"启用 / 停用", L"关闭"};
        const int ids[] = {IDC_PLG_RELOAD, IDC_PLG_OPENDIR, IDC_PLG_TOGGLE, IDC_PLG_CLOSE};
        int bx = 16;
        for (int i = 0; i < 4; ++i) {
            HWND b = CreateWindowW(L"BUTTON", labels[i], WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                                   bx, 424, 132, 34, hwnd, (HMENU)(INT_PTR)ids[i], nullptr, nullptr);
            (void)b;
            bx += 140;
        }

        plg_fill(hwnd, ctx);
        return 0;
    }

    case WM_ERASEBKGND: {
        HDC hdcE = (HDC)wp;
        RECT rcE;
        GetClientRect(hwnd, &rcE);
        HBRUSH br = CreateSolidBrush(kWinBg);
        FillRect(hdcE, &rcE, br);
        DeleteObject(br);
        return 1;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
        if (!ctx) break;
        bool primary = (dis->CtlID == IDC_PLG_CLOSE);
        COLORREF bg = primary ? kPrimary : kCardBg;
        COLORREF fg = primary ? RGB(255, 255, 255) : kTextMain;
        if (dis->itemState & ODS_SELECTED) bg = primary ? RGB(67, 56, 202) : RGB(226, 232, 240);
        draw_button(dis, bg, fg, primary ? CLR_INVALID : kBorder, ctx->f_bold, 8);
        return TRUE;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_PLG_CLOSE) { DestroyWindow(hwnd); return 0; }
        if (id == IDC_PLG_RELOAD) {
            PluginManager::get_instance().load_plugins("plugins");
            plg_fill(hwnd, ctx);
            return 0;
        }
        if (id == IDC_PLG_OPENDIR) {
            CreateDirectoryW(L"plugins", nullptr);
            ShellExecuteW(nullptr, L"open", L"plugins", nullptr, nullptr, SW_SHOW);
            return 0;
        }
        if (id == IDC_PLG_TOGGLE) {
            HWND lv = GetDlgItem(hwnd, IDC_PLG_LIST);
            int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
            if (sel < 0 || sel >= (int)ctx->items.size()) {
                MessageBoxW(hwnd, L"请先在列表里选中一个插件。", L"提示", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            bool now = ctx->items[sel].enabled;
            PluginManager::get_instance().set_plugin_enabled((size_t)sel, !now);
            plg_fill(hwnd, ctx);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (ctx) {
            if (ctx->f_body) DeleteObject(ctx->f_body);
            if (ctx->f_bold) DeleteObject(ctx->f_bold);
            ctx->f_body = ctx->f_bold = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ===========================================================================
// 使用说明
// ===========================================================================
const wchar_t* kHelpClass = L"TabMind_HelpWnd";
const int IDC_HELP_TEXT  = 2201;
const int IDC_HELP_CLOSE = 2202;

const wchar_t* kHelpText =
L"【启动流程】\r\n"
L"  1. 先在 NapCat / LLOneBot 里登录机器人 QQ，确认 WebSocket 端口与「OneBot WS 地址」一致\r\n"
L"  2. 填好 API 接口地址与 API Key，点「测试 API 连通性」确认能通\r\n"
L"  3. 点「启动 AI 智能机器人」，状态变绿即上线\r\n"
L"\r\n"
L"【群聊与私聊】\r\n"
L"  · 私聊：默认每条消息都会回复\r\n"
L"  · 群聊：默认需要 @机器人 或以「触发前缀」开头（如 /chat 你好）才响应\r\n"
L"  · 想放宽：在「高级设置 → 行为开关」里打开「宽松接话」，由模型自行判断要不要说话\r\n"
L"  · 想让某些插件不用 @：在主界面「免@插件」里填插件关键词（逗号分隔）\r\n"
L"\r\n"
L"【插件】\r\n"
L"  · 把插件 DLL 放进程序目录下的 plugins 文件夹，点「重载插件」即可生效\r\n"
L"  · 点「插件管理」可以查看已加载插件、单独启用或停用\r\n"
L"  · 插件返回 true 会拦截消息、不消耗 AI Token；返回 false 则继续交给 AI\r\n"
L"\r\n"
L"【常用指令（由插件提供，示例）】\r\n"
L"  · /chat 内容 —— 强制触发 AI 回复\r\n"
L"  · /reset 或「重置」—— 清空当前会话记忆\r\n"
L"\r\n"
L"【配置文件】\r\n"
L"  · 所有设置保存在同目录 config.json，界面上点「保存所有配置」即可写入\r\n"
L"  · 高级项（温度、记忆、冷却、认证方式等）在「高级设置」窗口里\r\n"
L"\r\n"
L"【常见问题】\r\n"
L"  · 连不上 OneBot：检查端口、access_token、防火墙；NapCat 是否已开启正向 WebSocket\r\n"
L"  · AI 返回空内容：API 接口不支持该模型，或思考模式吃掉了 max_tokens（可在高级设置里关闭思考）\r\n"
L"  · 图片识别不了：确认视觉模型名称正确，且接口支持多模态\r\n"
L"  · 群聊不回复：多半是没 @机器人 又没命中前缀（见上方群聊规则）\r\n";

struct HelpCtx {
    HFONT f_body = nullptr;
    HFONT f_bold = nullptr;
};

LRESULT CALLBACK help_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HelpCtx* ctx = (HelpCtx*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        ctx = (HelpCtx*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);
        ctx->f_body = mk_font(13, FW_NORMAL, L"Microsoft YaHei UI");
        ctx->f_bold = mk_font(15, FW_BOLD, L"Microsoft YaHei UI");

        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", kHelpText,
                                    WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_READONLY |
                                    ES_AUTOVSCROLL | WS_VSCROLL | ES_NOHIDESEL,
                                    16, 16, 560, 430, hwnd, (HMENU)(INT_PTR)IDC_HELP_TEXT, nullptr, nullptr);
        SendMessageW(edit, WM_SETFONT, (WPARAM)ctx->f_body, TRUE);
        SendMessageW(edit, EM_SETSEL, 0, 0);

        CreateWindowW(L"BUTTON", L"知道了", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                      456, 458, 120, 34, hwnd, (HMENU)(INT_PTR)IDC_HELP_CLOSE, nullptr, nullptr);
        return 0;
    }

    case WM_ERASEBKGND: {
        HDC hdcE = (HDC)wp;
        RECT rcE;
        GetClientRect(hwnd, &rcE);
        HBRUSH br = CreateSolidBrush(kWinBg);
        FillRect(hdcE, &rcE, br);
        DeleteObject(br);
        return 1;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
        if (!ctx || dis->CtlID != IDC_HELP_CLOSE) break;
        COLORREF bg = (dis->itemState & ODS_SELECTED) ? RGB(67, 56, 202) : kPrimary;
        draw_button(dis, bg, RGB(255, 255, 255), CLR_INVALID, ctx->f_bold, 8);
        return TRUE;
    }

    case WM_CTLCOLOREDIT: {
        SetBkColor((HDC)wp, kCardBg);
        SetTextColor((HDC)wp, kTextMain);
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }

    case WM_COMMAND:
        if (LOWORD(wp) == IDC_HELP_CLOSE) { DestroyWindow(hwnd); return 0; }
        break;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (ctx) {
            if (ctx->f_body) DeleteObject(ctx->f_body);
            if (ctx->f_bold) DeleteObject(ctx->f_bold);
            ctx->f_body = ctx->f_bold = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// 统一的弹窗创建
HWND create_modal(HWND parent, HINSTANCE inst, const wchar_t* cls, const wchar_t* title,
                  WNDPROC proc, void* ctx, int w, int h) {
    ensure_class(cls, proc, inst);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, cls, title,
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                               0, 0, w, h, parent, nullptr, inst, ctx);
    if (!dlg) return nullptr;
    center_on(dlg, parent, w, h);
    ShowWindow(dlg, SW_SHOW);
    return dlg;
}

} // namespace

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
namespace ui_dialogs {

bool show_advanced_settings(HWND parent, AppConfig& cfg, HINSTANCE inst) {
    AdvCtx ctx;
    ctx.cfg = &cfg;
    HWND dlg = create_modal(parent, inst, kAdvClass, L"高级设置 — 采样 / 记忆 / 视觉 / 行为",
                            adv_proc, &ctx, 580, 596);
    if (!dlg) return false;
    run_modal(parent, dlg);
    return ctx.saved;
}

void show_plugin_manager(HWND parent, HINSTANCE inst) {
    PlgCtx ctx;
    HWND dlg = create_modal(parent, inst, kPluginClass, L"插件管理",
                            plg_proc, &ctx, 600, 560);
    if (!dlg) return;
    run_modal(parent, dlg);
}

void show_help(HWND parent, HINSTANCE inst) {
    HelpCtx ctx;
    HWND dlg = create_modal(parent, inst, kHelpClass, L"使用说明",
                            help_proc, &ctx, 600, 528);
    if (!dlg) return;
    run_modal(parent, dlg);
}

} // namespace ui_dialogs
