// ===========================================================================
// TabMind 前端 v4 · 主窗口实现（单 HWND 全自绘）
// ===========================================================================
#include "ui_window.hpp"
#include "ui_core.hpp"
#include "ui_dialogs.hpp"
#include "config.hpp"
#include "bot_client.hpp"
#include "plugin_manager.hpp"

#include <shellapi.h>
#include <imm.h>
#include <windowsx.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdio>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "imm32.lib")

#define WM_APP_LOG      (WM_APP + 1)
#define WM_APP_TESTDONE (WM_APP + 2)

#define IDT_CARET  1
#define IDT_STATUS 2

enum { P_STATUS = 0, P_AI, P_QQ, P_PLUGIN, P_ACTION, P_LOG, P_COUNT };

static const wchar_t* kMenu[P_COUNT] = {
    L"运行状态", L"AI 接口配置", L"OneBot / QQ 接入",
    L"扩展插件", L"快捷操作", L"运行日志"
};

// ---------------------------------------------------------------------------
// 双缓冲池
// ---------------------------------------------------------------------------
namespace {
struct BufSlot {
    HDC hdc = nullptr;
    HBITMAP bmp = nullptr;
    int w = 0, h = 0;
    bool used = false;
};
BufSlot g_bufs[3];

struct PB {
    BufSlot* s = nullptr;
    RECT rc{};
    HDC mem() const { return s ? s->hdc : nullptr; }
    bool begin(HDC ref, const RECT& r) {
        int w = r.right - r.left, h = r.bottom - r.top;
        if (w <= 0 || h <= 0) return false;
        s = nullptr;
        for (BufSlot& x : g_bufs) if (!x.used) { s = &x; break; }
        if (!s) return false;
        if (!s->hdc) { s->hdc = CreateCompatibleDC(ref); if (!s->hdc) { s = nullptr; return false; } }
        if (!s->bmp || s->w < w || s->h < h) {
            HBITMAP nb = CreateCompatibleBitmap(ref, w, h);
            if (!nb) return false;
            if (s->bmp) DeleteObject(s->bmp);
            s->bmp = nb; s->w = w; s->h = h;
        }
        SelectObject(s->hdc, s->bmp);
        SetWindowOrgEx(s->hdc, r.left, r.top, NULL);
        s->used = true;
        rc = r;
        return true;
    }
    void end(HDC dst) {
        if (!s) return;
        BitBlt(dst, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
               s->hdc, rc.left, rc.top, SRCCOPY);
        s->used = false;
        s = nullptr;
    }
};

static bool in_rect(const RECT& r, POINT p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}
static int  RW(const RECT& r) { return r.right - r.left; }
static int  RH(const RECT& r) { return r.bottom - r.top; }

static void invalidate_ctl(HWND h, const RECT& r) {
    if (h) InvalidateRect(h, &r, FALSE);
}

// 日志 emoji 清理（自绘字体下会撑坏行距）
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
    s.swap(out);
}
} // namespace

// ---------------------------------------------------------------------------
// 面板
// ---------------------------------------------------------------------------
struct Panel {
    int  kind = 0;
    bool open = true;
    RECT rc{};
    std::vector<std::unique_ptr<W>> ws;

    LogView* logview = nullptr;
    Combo*   combo = nullptr;
    Field*   textarea = nullptr;
};

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct MainWindow::Impl {
    HWND hwnd = nullptr;
    HINSTANCE inst = nullptr;
    App app;
    AppConfig cfg;

    std::vector<std::unique_ptr<Panel>> panels;
    int zorder[P_COUNT] = { 0, 1, 2, 3, 4, 5 };
    Combo* open_combo = nullptr;

    // 字段
    Field* f_base_url = nullptr;
    Field* f_api_key = nullptr;
    Field* f_max_tokens = nullptr;
    Field* f_rounds = nullptr;
    Field* f_system = nullptr;
    Field* f_ws_url = nullptr;
    Field* f_bot_qq = nullptr;
    Field* f_token = nullptr;
    Field* f_prefixes = nullptr;
    Field* f_at_exempt = nullptr;
    Combo* c_model = nullptr;
    Check* ck_require_at = nullptr;
    Label* lb_plugin = nullptr;
    Button* btn_toggle = nullptr;
    LogView* logview = nullptr;

    Field* focus_field = nullptr;

    // 拖动（屏幕坐标，面板位置直接从数据算，不存在坐标系抵消问题）
    int   drag_mode = 0;        // 0 无 / 1 移动 / 2 缩放
    int   drag_kind = -1;
    POINT drag_origin{};
    RECT  drag_rect0{};

    int hover_menu = -1;
    bool hover_theme = false;
    int last_w_ = 0, last_h_ = 0;
    // 用户是否自定义过布局。false 表示还在用默认三分栏，
    // 此时窗口尺寸变化要重新按比例铺满；一旦拖过/缩放过就固定下来。
    bool layout_custom = false;

    std::mutex log_mtx;
    std::vector<std::string> pending;
    int  msg_count = 0;
    bool status_dirty = true;
    std::unique_ptr<BotClient> bot;
    bool bot_running = false;
    std::vector<PluginInfo> plugins;

    void make_fonts();
    void build_panels();
    void layout_all();
    void layout_panel(Panel& p);
    void apply_default_layout();
    void clamp_panel(Panel& p);

    void invalidate_all_() { if (hwnd) InvalidateRect(hwnd, NULL, FALSE); }
    static void union_rect_(RECT* dst, const RECT& src) {
        if (dst->right <= dst->left || dst->bottom <= dst->top) { *dst = src; return; }
        dst->left = (std::min)(dst->left, src.left);
        dst->top = (std::min)(dst->top, src.top);
        dst->right = (std::max)(dst->right, src.right);
        dst->bottom = (std::max)(dst->bottom, src.bottom);
    }

    RECT menu_rect(int i) const;
    int  menu_hit(POINT p) const;
    RECT theme_rect() const;
    RECT title_rect(const Panel& p) const;
    RECT close_rect(const Panel& p) const;
    RECT grip_rect(const Panel& p) const;

    void paint(HDC dc, const RECT& dirty);
    void paint_sidebar(HDC dc, int W, int H);
    void paint_panel(HDC dc, Panel& p);
    void paint_status_card(HDC dc, Panel& p);
    void paint_plugin_card(HDC dc, Panel& p);

    Panel* panel_at(POINT pt);
    void   raise(int kind);
    void   toggle(int kind);

    void on_move(POINT p);
    void on_down(POINT p);
    void on_up(POINT p);
    void on_wheel(int delta, POINT p);
    bool on_keydown(int vk, bool ctrl);
    bool on_char(wchar_t c);
    void set_focus(Field* f);
    // 自绘输入框必须手动告诉 IME 光标在哪，否则中文候选窗会飘到窗口左上角
    void sync_ime_pos();

    void sync_ui_from_cfg();
    void sync_cfg_from_ui();
    void save_layout();
    void load_layout();
    void refresh_plugins();
    void toggle_bot();
    void save_config();
    void test_api();
    void pump_logs();
    void push_log(const std::string& s);

    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
};

// ---------------------------------------------------------------------------
// 字体
// ---------------------------------------------------------------------------
void MainWindow::Impl::make_fonts() {
    auto mk = [](int h, int w, const wchar_t* face) {
        return CreateFontW(-h, 0, 0, 0, w, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
    };
    app.f_brand = mk(19, FW_BOLD,   L"Microsoft YaHei UI");
    app.f_h2    = mk(14, FW_BOLD,   L"Microsoft YaHei UI");
    app.f_body  = mk(14, FW_NORMAL, L"Microsoft YaHei UI");
    app.f_bold  = mk(14, FW_BOLD,   L"Microsoft YaHei UI");
    app.f_small = mk(12, FW_NORMAL, L"Microsoft YaHei UI");
    app.f_log   = mk(13, FW_NORMAL, L"Microsoft YaHei UI");
    app.inst    = inst;
}

// ---------------------------------------------------------------------------
// 构建面板
// ---------------------------------------------------------------------------
void MainWindow::Impl::build_panels() {
    panels.clear();
    for (int k = 0; k < P_COUNT; ++k) {
        auto p = std::make_unique<Panel>();
        p->kind = k;
        panels.push_back(std::move(p));
    }

    // ---------- 运行状态 ----------
    // 内容全部在 paint_status_card 里直绘（胶囊），不需要控件

    // ---------- AI 配置 ----------
    {
        Panel& p = *panels[P_AI];
        auto add_label = [&](const wchar_t* s) {
            auto l = std::make_unique<Label>();
            l->text = s; l->font = app.f_small;
            p.ws.push_back(std::move(l));
        };
        auto add_field = [&](bool pwd, const wchar_t* ph) -> Field* {
            auto f = std::make_unique<Field>();
            f->password = pwd; f->placeholder = ph ? ph : L"";
            Field* raw = f.get();
            p.ws.push_back(std::move(f));
            return raw;
        };
        add_label(L"API 接口地址 (Base URL)");
        f_base_url = add_field(false, L"https://api.example.com/v1");
        add_label(L"API 密钥 (API Key)");
        f_api_key = add_field(true, L"sk-...");
        add_label(L"模型");
        add_label(L"Max Tokens");
        add_label(L"上下文轮数");
        {
            auto c = std::make_unique<Combo>();
            c->items = { L"deepseek-chat", L"deepseek-reasoner", L"deepseek-v4-flash",
                         L"gpt-4o-mini", L"gpt-4o" };
            c_model = c.get();
            p.ws.push_back(std::move(c));
        }
        f_max_tokens = add_field(false, L"1000");
        f_rounds = add_field(false, L"10");
        add_label(L"系统提示词 / 人设");
        {
            auto f = std::make_unique<Field>();
            f->multiline = true;
            f_system = f.get();
            p.ws.push_back(std::move(f));
        }
    }

    // ---------- OneBot / QQ ----------
    {
        Panel& p = *panels[P_QQ];
        auto add_label = [&](const wchar_t* s) {
            auto l = std::make_unique<Label>();
            l->text = s; l->font = app.f_small;
            p.ws.push_back(std::move(l));
        };
        auto add_field = [&](bool pwd, const wchar_t* ph) -> Field* {
            auto f = std::make_unique<Field>();
            f->password = pwd; f->placeholder = ph ? ph : L"";
            Field* raw = f.get();
            p.ws.push_back(std::move(f));
            return raw;
        };
        add_label(L"OneBot WebSocket 地址");
        f_ws_url = add_field(false, L"ws://127.0.0.1:3001");
        add_label(L"机器人 QQ 号");
        add_label(L"Access Token（无则留空）");
        f_bot_qq = add_field(false, L"10001");
        f_token = add_field(true, L"");
        add_label(L"触发前缀词（逗号分隔）");
        f_prefixes = add_field(false, L"/chat, /ai");
        {
            auto c = std::make_unique<Check>();
            c->text = L"群聊必须 @机器人 或命中前缀";
            ck_require_at = c.get();
            p.ws.push_back(std::move(c));
        }
        add_label(L"免 @ 插件（逗号分隔）");
        f_at_exempt = add_field(false, L"waifu_manager, gallery_manager");
    }

    // ---------- 插件 ----------
    {
        Panel& p = *panels[P_PLUGIN];
        auto l = std::make_unique<Label>();
        l->text = L"正在加载插件...";
        l->font = app.f_body;
        l->color = theme().text_main;
        lb_plugin = l.get();
        p.ws.push_back(std::move(l));

        auto mk_btn = [&](const wchar_t* s, std::function<void()> fn) {
            auto b = std::make_unique<Button>();
            b->text = s; b->click = std::move(fn);
            p.ws.push_back(std::move(b));
        };
        mk_btn(L"重载插件", [this]() {
            PluginManager::get_instance().load_plugins("plugins");
            refresh_plugins();
            push_log("插件已重新加载");
        });
        mk_btn(L"插件目录", []() {
            CreateDirectoryW(L"plugins", NULL);
            ShellExecuteW(NULL, L"open", L"plugins", NULL, NULL, SW_SHOW);
        });
        mk_btn(L"插件管理", [this]() {
            ui_dialogs::show_plugin_manager(hwnd, inst);
            refresh_plugins();
        });
    }

    // ---------- 快捷操作 ----------
    {
        Panel& p = *panels[P_ACTION];
        auto mk_btn = [&](const wchar_t* s, bool primary, std::function<void()> fn) -> Button* {
            auto b = std::make_unique<Button>();
            b->text = s; b->primary = primary; b->click = std::move(fn);
            Button* raw = b.get();
            p.ws.push_back(std::move(b));
            return raw;
        };
        btn_toggle = mk_btn(L"启动机器人", true, [this]() { toggle_bot(); });
        mk_btn(L"保存配置", false, [this]() { save_config(); });
        mk_btn(L"测试接口", false, [this]() { test_api(); });
        mk_btn(L"高级设置", false, [this]() {
            if (ui_dialogs::show_advanced_settings(hwnd, cfg, inst)) {
                cfg.save_to_file("config.json");
                status_dirty = true;
                push_log("高级设置已保存");
            }
        });
        mk_btn(L"使用说明", false, [this]() {
            ui_dialogs::show_help(hwnd, inst);
        });
        mk_btn(L"重置布局", false, [this]() {
            layout_custom = false;
            apply_default_layout();
            layout_all();
            save_layout();
            invalidate_all_();
            push_log("布局已重置（恢复自适应三分栏）");
        });
    }

    // ---------- 日志 ----------
    {
        Panel& p = *panels[P_LOG];
        {
            auto b = std::make_unique<Button>();
            b->text = L"清空";
            b->click = [this]() {
                if (logview) logview->lines.clear();
                if (logview) logview->top = 0;
                push_log("日志已清空");
            };
            p.ws.push_back(std::move(b));
        }
        {
            auto v = std::make_unique<LogView>();
            logview = v.get();
            p.logview = logview;
            p.ws.push_back(std::move(v));
        }
    }
}

// ---------------------------------------------------------------------------
// 布局
// ---------------------------------------------------------------------------
RECT MainWindow::Impl::menu_rect(int i) const {
    return RECT{ 12, 100 + i * 56, U::SidebarW - 12, 100 + i * 56 + 50 };
}
int MainWindow::Impl::menu_hit(POINT p) const {
    if (p.x < 12 || p.x >= U::SidebarW - 12) return -1;
    for (int i = 0; i < P_COUNT; ++i) if (in_rect(menu_rect(i), p)) return i;
    return -1;
}
RECT MainWindow::Impl::theme_rect() const {
    RECT cr;
    GetClientRect(hwnd, &cr);
    int y = cr.bottom - 46;
    if (y < 420) y = 420;
    return RECT{ 14, y, U::SidebarW - 14, y + 34 };
}
RECT MainWindow::Impl::title_rect(const Panel& p) const {
    return RECT{ p.rc.left + U::Pad, p.rc.top + 6,
                 p.rc.right - 40, p.rc.top + U::TitleH };
}
RECT MainWindow::Impl::close_rect(const Panel& p) const {
    return RECT{ p.rc.right - 30, p.rc.top + 8, p.rc.right - 12, p.rc.top + 26 };
}
RECT MainWindow::Impl::grip_rect(const Panel& p) const {
    return RECT{ p.rc.right - U::Grip, p.rc.bottom - U::Grip, p.rc.right, p.rc.bottom };
}

void MainWindow::Impl::clamp_panel(Panel& p) {
    RECT cr;
    GetClientRect(hwnd, &cr);
    int w = RW(p.rc), h = RH(p.rc);
    if (w < U::MinPanelW) w = U::MinPanelW;
    if (h < U::MinPanelH) h = U::MinPanelH;
    int minX = U::SidebarW + 12, minY = 8;
    int maxX = cr.right - 8 - w;
    int maxY = cr.bottom - 8 - h;
    if (maxX < minX) maxX = minX;
    if (maxY < minY) maxY = minY;
    if (p.rc.left < minX) p.rc.left = minX;
    if (p.rc.top < minY) p.rc.top = minY;
    if (p.rc.left > maxX) p.rc.left = maxX;
    if (p.rc.top > maxY) p.rc.top = maxY;
    p.rc.right = p.rc.left + w;
    p.rc.bottom = p.rc.top + h;
}

void MainWindow::Impl::apply_default_layout() {
    RECT cr;
    GetClientRect(hwnd, &cr);
    int x0 = U::SidebarW + 12;
    int availW = cr.right - x0 - 12;
    int availH = cr.bottom - 24;
    int col = availW / 3 - 10;
    if (col < 260) col = 260;

    auto set = [&](int k, int x, int y, int w, int h) {
        panels[k]->rc = RECT{ x, y, x + w, y + h };
        panels[k]->open = true;
    };
    set(P_STATUS, x0, 12, col, 120);
    set(P_AI,     x0, 140, col, availH - 152);
    set(P_PLUGIN, x0 + col + 10, 12, col, 246);
    set(P_QQ,     x0 + col + 10, 266, col, availH - 278);
    set(P_ACTION, x0 + (col + 10) * 2, 12, col, 246);
    set(P_LOG,    x0 + (col + 10) * 2, 266, col, availH - 278);
    for (int k = 0; k < P_COUNT; ++k) { zorder[k] = k; clamp_panel(*panels[k]); }
}

void MainWindow::Impl::layout_panel(Panel& p) {
    const int L = p.rc.left + U::Pad;
    const int R = p.rc.right - U::Pad;
    const int CW = R - L;
    if (CW <= 20) return;
    int y = p.rc.top + U::TitleH + 8;

    if (p.kind == P_AI) {
        auto it = p.ws.begin();
        auto place = [&](int x, int yy, int w, int h) -> RECT {
            return RECT{ x, yy, x + w, yy + h };
        };
        // 顺序与 build 时一致：label, base_url, label, api_key, label×3, combo, max_tokens, rounds, label, textarea
        (*it)->rc = place(L, y, CW, U::LabelH); ++it;
        (*it)->rc = place(L, y + U::LabelH + 2, CW, U::InputH); ++it;
        y += U::LabelH + 2 + U::InputH + U::RowGap;

        (*it)->rc = place(L, y, CW, U::LabelH); ++it;
        (*it)->rc = place(L, y + U::LabelH + 2, CW, U::InputH); ++it;
        y += U::LabelH + 2 + U::InputH + U::RowGap;

        int wModel = CW * 46 / 100;
        int wTok   = CW * 24 / 100;
        int wRound = CW - wModel - wTok - 16;
        if (wRound < 52) wRound = 52;
        int xTok = L + wModel + 8;
        int xRound = xTok + wTok + 8;

        (*it)->rc = place(L, y, wModel, U::LabelH); ++it;          // 模型
        (*it)->rc = place(xTok, y, wTok, U::LabelH); ++it;         // Max Tokens
        (*it)->rc = place(xRound, y, wRound, U::LabelH); ++it;     // 轮数
        (*it)->rc = place(L, y + U::LabelH + 2, wModel, U::InputH); ++it;
        (*it)->rc = place(xTok, y + U::LabelH + 2, wTok, U::InputH); ++it;
        (*it)->rc = place(xRound, y + U::LabelH + 2, wRound, U::InputH); ++it;
        y += U::LabelH + 2 + U::InputH + U::RowGap;

        (*it)->rc = place(L, y, CW, U::LabelH); ++it;
        int hArea = p.rc.bottom - U::Pad - (y + U::LabelH + 2);
        if (hArea < 60) hArea = 60;
        (*it)->rc = place(L, y + U::LabelH + 2, CW, hArea); ++it;
    } else if (p.kind == P_QQ) {
        auto it = p.ws.begin();
        auto place = [&](int x, int yy, int w, int h) -> RECT {
            return RECT{ x, yy, x + w, yy + h };
        };
        (*it)->rc = place(L, y, CW, U::LabelH); ++it;
        (*it)->rc = place(L, y + U::LabelH + 2, CW, U::InputH); ++it;
        y += U::LabelH + 2 + U::InputH + U::RowGap;

        int wBot = CW * 42 / 100;
        int wTok = CW - wBot - 8;
        (*it)->rc = place(L, y, wBot, U::LabelH); ++it;
        (*it)->rc = place(L + wBot + 8, y, wTok, U::LabelH); ++it;
        (*it)->rc = place(L, y + U::LabelH + 2, wBot, U::InputH); ++it;
        (*it)->rc = place(L + wBot + 8, y + U::LabelH + 2, wTok, U::InputH); ++it;
        y += U::LabelH + 2 + U::InputH + U::RowGap;

        (*it)->rc = place(L, y, CW, U::LabelH); ++it;
        (*it)->rc = place(L, y + U::LabelH + 2, CW, U::InputH); ++it;
        y += U::LabelH + 2 + U::InputH + U::RowGap;

        (*it)->rc = place(L, y, CW, 22); ++it;
        y += 26;

        (*it)->rc = place(L, y, CW, U::LabelH); ++it;
        (*it)->rc = place(L, y + U::LabelH + 2, CW, U::InputH); ++it;
    } else if (p.kind == P_PLUGIN) {
        auto it = p.ws.begin();
        (*it)->rc = RECT{ L, y, R, y + 22 }; ++it;
        int by = y + 30;
        int bw = (CW - 16) / 3;
        if (bw < 62) bw = 62;
        for (int i = 0; i < 3; ++i, ++it) {
            (*it)->rc = RECT{ L + i * (bw + 8), by, L + i * (bw + 8) + bw, by + U::BtnH };
        }
    } else if (p.kind == P_ACTION) {
        int by = y;
        for (auto& w : p.ws) {
            Button* b = dynamic_cast<Button*>(w.get());
            if (!b) continue;
            if (b == btn_toggle) {
                b->rc = RECT{ L, by, R, by + 44 };
                by += 54;
            } else {
                int half = (CW - 8) / 2;
                // 成对排列
                static const int seq[] = { 0, 1, 0, 1, 2 };
                (void)seq;
                b->rc = RECT{ L, by, L + half, by + U::BtnH };
                // 由下面的配对逻辑覆盖
            }
        }
        // 明确排布：保存/测试、高级/说明、重置（整行）
        std::vector<Button*> bs;
        for (auto& w : p.ws) {
            Button* b = dynamic_cast<Button*>(w.get());
            if (b) bs.push_back(b);
        }
        int half = (CW - 8) / 2;
        by = y + 54;
        if (bs.size() >= 3) {
            bs[1]->rc = RECT{ L, by, L + half, by + U::BtnH };
            bs[2]->rc = RECT{ L + half + 8, by, R, by + U::BtnH };
        }
        by += U::BtnH + 10;
        if (bs.size() >= 5) {
            bs[3]->rc = RECT{ L, by, L + half, by + U::BtnH };
            bs[4]->rc = RECT{ L + half + 8, by, R, by + U::BtnH };
        }
        by += U::BtnH + 10;
        if (bs.size() >= 6) {
            bs[5]->rc = RECT{ L, by, R, by + U::BtnH };
        }
    } else if (p.kind == P_LOG) {
        // ws[0] = 清空按钮，ws[1] = LogView
        int bw = 52;
        if (p.ws.size() >= 1) p.ws[0]->rc = RECT{ R - bw, y - 2, R, y + 22 };
        int top = y + 26;
        int hh = p.rc.bottom - U::Pad - top;
        if (hh < 50) hh = 50;
        if (p.ws.size() >= 2) p.ws[1]->rc = RECT{ L - 4, top, R + 4, top + hh };
        if (logview) logview->clamp_top();
    }
}

void MainWindow::Impl::layout_all() {
    for (auto& p : panels) layout_panel(*p);
    if (logview) logview->clamp_top();
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------
void MainWindow::Impl::paint_sidebar(HDC dc, int W, int H) {
    (void)W;
    const ThemePalette& t = theme();
    UiGdiCache& g = ui_gdi();

    RECT side = { 0, 0, U::SidebarW, H };
    FillRect(dc, &side, g.br_card);
    {
        HGDIOBJ op = SelectObject(dc, g.pen_border_soft);
        MoveToEx(dc, U::SidebarW, 0, NULL);
        LineTo(dc, U::SidebarW, H);
        SelectObject(dc, op);
    }

    // 品牌
    RECT logo = { 14, 14, 46, 46 };
    draw::round_rect(dc, logo, 9, g.br_primary, g.pen_null);
    draw::text(dc, app.f_brand, RGB(255, 255, 255), logo, L"T",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    draw::text(dc, app.f_brand, t.text_main, RECT{ 54, 12, U::SidebarW - 8, 38 },
               L"TabMind", DT_LEFT | DT_TOP | DT_SINGLELINE);
    draw::text(dc, app.f_small, t.text_sub, RECT{ 56, 36, U::SidebarW - 6, 54 },
               L"AI Agent Console", DT_LEFT | DT_TOP | DT_SINGLELINE);

    // 状态胶囊
    bool run = bot_running;
    RECT pill = { 14, 58, U::SidebarW - 14, 86 };
    draw::round_rect(dc, pill, 14, run ? g.br_success_soft : g.br_danger_soft, g.pen_null);
    {
        HGDIOBJ ob = SelectObject(dc, run ? g.br_success : g.br_danger);
        HGDIOBJ op = SelectObject(dc, g.pen_null);
        Ellipse(dc, 26, 68, 36, 78);
        SelectObject(dc, ob);
        SelectObject(dc, op);
    }
    {
        std::wstring s = run ? L"运行中" : L"已停止";
        s += L" · 已回复 " + std::to_wstring(msg_count);
        draw::text(dc, app.f_small, run ? t.success_text : t.danger,
                   RECT{ 44, 58, U::SidebarW - 16, 86 }, s,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // 菜单
    for (int i = 0; i < P_COUNT; ++i) {
        RECT r = menu_rect(i);
        bool open = panels[i]->open;
        bool hov = (hover_menu == i);
        if (open || hov) {
            draw::round_rect(dc, r, 9,
                             hov ? g.br_primary_soft : g.br_card,
                             hov ? g.pen_primary : g.pen_border);
        }
        if (open) {
            RECT bar = { r.left + 2, r.top + 11, r.left + 5, r.bottom - 11 };
            draw::round_rect(dc, bar, 2, g.br_primary, g.pen_null);
        }
        draw::text(dc, (open || hov) ? app.f_bold : app.f_body,
                   open ? t.primary : t.text_main,
                   RECT{ r.left + 14, r.top, r.right - 24, r.bottom },
                   kMenu[i], DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (open) {
            draw::text(dc, app.f_small, t.primary,
                       RECT{ r.right - 26, r.top, r.right - 8, r.bottom },
                       L"\u25CF", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    // 提示
    draw::text(dc, app.f_small, t.text_faint,
               RECT{ 14, H - 96, U::SidebarW - 14, H - 80 },
               L"拖标题移动 · 右下角缩放",
               DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

    // 主题按钮
    RECT tb = theme_rect();
    draw::round_rect(dc, tb, 9,
                     hover_theme ? g.br_field : g.br_card, g.pen_border);
    std::wstring ts = std::wstring(L"主题 · ") + t.name;
    draw::text(dc, app.f_small, t.primary, tb, ts,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void MainWindow::Impl::paint_status_card(HDC dc, Panel& p) {
    const ThemePalette& t = theme();
    UiGdiCache& g = ui_gdi();
    int L = p.rc.left + U::Pad;
    int W = RW(p.rc) - U::Pad * 2;
    int y = p.rc.top + U::TitleH + 4;
    int h = 30;
    if (y + h > p.rc.bottom - 8) h = p.rc.bottom - 8 - y;
    if (h < 18) h = 18;

    int w1 = W * 30 / 100;
    int w2 = W * 42 / 100;
    int w3 = W - w1 - w2 - 12;
    if (w3 < 50) w3 = 50;

    bool run = bot_running;
    RECT r1 = { L, y, L + w1, y + h };
    RECT r2 = { L + w1 + 6, y, L + w1 + 6 + w2, y + h };
    RECT r3 = { L + w1 + w2 + 12, y, L + w1 + w2 + 12 + w3, y + h };

    draw::pill(dc, r1, run ? g.br_success_soft : g.br_danger_soft,
               run ? t.success_text : t.danger,
               run ? L"\u25CF 运行中" : L"\u25CF 已停止", app.f_small);
    draw::pill(dc, r2, g.br_primary_soft, t.primary,
               L"模型 " + app.model_name, app.f_small);
    draw::pill(dc, r3, g.br_field, t.text_main,
               L"已回复 " + std::to_wstring(msg_count), app.f_small);
}

void MainWindow::Impl::paint_plugin_card(HDC dc, Panel& p) {
    const ThemePalette& t = theme();
    UiGdiCache& g = ui_gdi();
    int L = p.rc.left + U::Pad;
    int R = p.rc.right - U::Pad;
    int W = R - L;
    int listTop = p.rc.top + U::TitleH + 8 + 34;
    const int rowH = 26;

    int avail = p.rc.bottom - U::Pad - 8 - listTop;
    if (avail < rowH) return;
    int rows = avail / rowH;
    if (rows < 1) rows = 1;

    if (plugins.empty()) {
        draw::text(dc, app.f_small, t.text_faint,
                   RECT{ L, listTop, R, listTop + 20 },
                   L"暂无插件 · 把 DLL 放进 plugins 目录",
                   DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    int shown = 0;
    for (auto& pl : plugins) {
        if (shown >= rows) break;
        int y = listTop + shown * rowH;
        RECT row = { L, y, R, y + rowH - 4 };
        draw::round_rect(dc, row, 7, g.br_field, g.pen_null);
        {
            HGDIOBJ ob = SelectObject(dc, pl.enabled ? g.br_success : g.br_text_faint);
            HGDIOBJ op = SelectObject(dc, g.pen_null);
            Ellipse(dc, L + 8, y + 7, L + 14, y + 13);
            SelectObject(dc, ob);
            SelectObject(dc, op);
        }
        draw::text(dc, app.f_body, pl.enabled ? t.text_main : t.text_sub,
                   RECT{ L + 20, y + 1, R - 56, y + rowH - 4 },
                   u2w(pl.name), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        draw::text(dc, app.f_small, t.text_faint,
                   RECT{ R - 54, y + 1, R - 6, y + rowH - 4 },
                   u2w("v" + pl.version),
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        ++shown;
    }
    if ((int)plugins.size() > shown) {
        std::wstring more = L"还有 " + std::to_wstring(plugins.size() - shown) + L" 个";
        draw::text(dc, app.f_small, t.text_faint,
                   RECT{ L, listTop + shown * rowH, R, listTop + shown * rowH + 16 },
                   more, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }
}

void MainWindow::Impl::paint_panel(HDC dc, Panel& p) {
    const ThemePalette& t = theme();
    UiGdiCache& g = ui_gdi();

    // 整个面板裁剪到「自身矩形 + 少量外扩」：
    // 自绘没有子窗口的天然裁剪，不设裁剪则控件文字会溢出到相邻面板；
    // 但完全贴边裁又会把卡片阴影切掉，所以向下/右各放 4px 给阴影。
    //
    // 必须用 IntersectClipRect 而不是 CreateRectRgn + SelectClipRgn：
    // 前者接受逻辑坐标并与当前裁剪区求交；后者把 region 当设备坐标、
    // 且会整个替换裁剪区。双缓冲里我们用 SetWindowOrgEx 平移了逻辑原点，
    // 用 SelectClipRgn 会让裁剪区整体错位 —— 拖动时表现为卡片内容被裁掉、
    // 只剩一块背景色，并在移动路径上留下背景色残影。
    int saved = SaveDC(dc);
    IntersectClipRect(dc, p.rc.left, p.rc.top, p.rc.right + 4, p.rc.bottom + 4);

    draw::card(dc, p.rc, U::Radius);

    // 标题
    draw::text(dc, app.f_h2, t.text_main,
               RECT{ p.rc.left + U::Pad, p.rc.top + 6, p.rc.right - 38, p.rc.top + U::TitleH },
               kMenu[p.kind], DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // 关闭按钮
    RECT cr = close_rect(p);
    draw::round_rect(dc, cr, 9, g.br_field, g.pen_null);
    {
        HGDIOBJ op = SelectObject(dc, g.pen_text_sub);
        int in = 5;
        MoveToEx(dc, cr.left + in, cr.top + in, NULL);
        LineTo(dc, cr.right - in, cr.bottom - in);
        MoveToEx(dc, cr.right - in, cr.top + in, NULL);
        LineTo(dc, cr.left + in, cr.bottom - in);
        SelectObject(dc, op);
    }

    // 缩放手柄
    if (RH(p.rc) > U::TitleH + 30) {
        int gx = p.rc.right - U::Grip, gy = p.rc.bottom - U::Grip;
        HGDIOBJ op = SelectObject(dc, g.pen_text_sub);
        for (int i = 1; i <= 3; ++i) {
            MoveToEx(dc, gx + i * 4, p.rc.bottom - 2, NULL);
            LineTo(dc, p.rc.right - 2, gy + i * 4);
        }
        SelectObject(dc, op);
    }

    // 内容
    if (p.kind == P_STATUS) {
        paint_status_card(dc, p);
    } else if (p.kind == P_PLUGIN) {
        paint_plugin_card(dc, p);
    }

    // 控件
    for (auto& w : p.ws) {
        if (!w->visible) continue;
        w->paint(dc, app);
    }

    RestoreDC(dc, saved);
}

void MainWindow::Impl::paint(HDC dc, const RECT& dirty) {
    const ThemePalette& t = theme();
    UiGdiCache& g = ui_gdi();
    RECT cr;
    GetClientRect(hwnd, &cr);

    FillRect(dc, &dirty, g.br_win);

    // 侧栏
    RECT side = { 0, 0, U::SidebarW, cr.bottom };
    RECT inter{};
    if (IntersectRect(&inter, &side, &dirty)) paint_sidebar(dc, cr.right, cr.bottom);

    // 面板（按 z 序，底 → 顶）
    for (int i = 0; i < P_COUNT; ++i) {
        Panel& p = *panels[zorder[i]];
        if (!p.open) continue;
        RECT hit{};
        if (!IntersectRect(&hit, &p.rc, &dirty)) continue;
        paint_panel(dc, p);
    }

    // 下拉弹层画在最上层
    if (open_combo && open_combo->open) {
        RECT pr = open_combo->popup_rect();
        RECT hit{};
        if (IntersectRect(&hit, &pr, &dirty)) open_combo->paint_popup(dc, app);
    }
    (void)t;
}

// ---------------------------------------------------------------------------
// 命中与交互
// ---------------------------------------------------------------------------
Panel* MainWindow::Impl::panel_at(POINT pt) {
    for (int i = P_COUNT - 1; i >= 0; --i) {
        Panel& p = *panels[zorder[i]];
        if (p.open && in_rect(p.rc, pt)) return &p;
    }
    return nullptr;
}

void MainWindow::Impl::raise(int kind) {
    if (zorder[P_COUNT - 1] == kind) return;
    int pos = -1;
    for (int i = 0; i < P_COUNT; ++i) if (zorder[i] == kind) { pos = i; break; }
    if (pos < 0) return;
    for (int i = pos; i < P_COUNT - 1; ++i) zorder[i] = zorder[i + 1];
    zorder[P_COUNT - 1] = kind;
    invalidate_all_();
}

void MainWindow::Impl::toggle(int kind) {
    panels[kind]->open = !panels[kind]->open;
    if (panels[kind]->open) raise(kind);
    save_layout();
    invalidate_all_();
}

void MainWindow::Impl::sync_ime_pos() {
    if (!hwnd || !focus_field) return;
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) return;
    POINT p = { focus_field->rc.left + 9, focus_field->rc.bottom };
    ClientToScreen(hwnd, &p);
    COMPOSITIONFORM cf{};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos = p;
    ImmSetCompositionWindow(himc, &cf);
    ImmReleaseContext(hwnd, himc);
}

void MainWindow::Impl::set_focus(Field* f) {
    if (focus_field == f) return;
    Field* old = focus_field;
    focus_field = f;
    for (auto& p : panels) {
        for (auto& w : p->ws) w->focused = (w.get() == (W*)f);
    }
    if (old) invalidate_ctl(hwnd, old->rc);
    if (f) invalidate_ctl(hwnd, f->rc);
    sync_ime_pos();
    SetTimer(hwnd, IDT_CARET, 530, NULL);
}

void MainWindow::Impl::on_move(POINT p) {
    // 拖动
    if (drag_mode != 0 && drag_kind >= 0) {
        POINT sp = p;
        ClientToScreen(hwnd, &sp);
        // 自愈：物理按键已弹起就结束拖动
        if ((GetKeyState(VK_LBUTTON) & 0x8000) == 0) {
            drag_mode = 0; drag_kind = -1;
            ReleaseCapture();
            return;
        }
        int dx = sp.x - drag_origin.x;
        int dy = sp.y - drag_origin.y;
        Panel& pn = *panels[drag_kind];
        RECT nr = drag_rect0;
        if (drag_mode == 1) {
            nr.left += dx; nr.right += dx;
            nr.top += dy; nr.bottom += dy;
        } else {
            nr.right += dx; nr.bottom += dy;
            if (RW(nr) < U::MinPanelW) nr.right = nr.left + U::MinPanelW;
            if (RH(nr) < U::MinPanelH) nr.bottom = nr.top + U::MinPanelH;
        }
        RECT old = pn.rc;
        pn.rc = nr;
        clamp_panel(pn);
        if (drag_mode == 2) {
            // 缩放：按新尺寸重新排布控件
            layout_panel(pn);
        } else {
            // 移动：把所有控件的矩形平移同样的位移。
            // 控件的 rc 是绝对客户区坐标，面板移动时必须一起挪，否则控件会
            // 留在原地 —— 而面板有边界裁剪，留在原地的控件直接被裁掉，
            // 看起来就是"拖卡片时里面的文字和输入框不动/消失"。
            // 用平移而不是 layout_panel：更快，而且不会触发布局里的 clamp 副作用。
            int ddx = pn.rc.left - old.left;
            int ddy = pn.rc.top - old.top;
            if (ddx || ddy) {
                for (auto& w : pn.ws) {
                    w->rc.left += ddx; w->rc.right += ddx;
                    w->rc.top += ddy;  w->rc.bottom += ddy;
                    w->hover = false;      // 拖动时鼠标在标题栏，控件不该保持高亮
                }
            }
            if (open_combo) { open_combo->open = false; open_combo = nullptr; }
        }
        // 只重绘"旧 ∪ 新"
        RECT uni = old;
        union_rect_(&uni, pn.rc);
        invalidate_ctl(hwnd, uni);
        return;
    }

    // 悬停
    int menu = menu_hit(p);
    bool th = in_rect(theme_rect(), p);
    if (menu != hover_menu || th != hover_theme) {
        RECT a = menu_rect(hover_menu >= 0 ? hover_menu : 0);
        RECT b = menu_rect(menu >= 0 ? menu : 0);
        RECT ua = theme_rect();
        if (menu != hover_menu) {
            if (hover_menu >= 0) invalidate_ctl(hwnd, a);
            if (menu >= 0) invalidate_ctl(hwnd, b);
        }
        if (th != hover_theme) invalidate_ctl(hwnd, ua);
        hover_menu = menu;
        hover_theme = th;
    }

    // 控件 hover
    Panel* pn = panel_at(p);
    for (auto& pa : panels) {
        for (auto& w : pa->ws) {
            bool h = (pa.get() == pn) && w->visible && in_rect(w->rc, p);
            if (h != w->hover) {
                w->hover = h;
                invalidate_ctl(hwnd, w->rc);
            }
        }
    }
}

void MainWindow::Impl::on_down(POINT p) {
    SetCapture(hwnd);

    // 下拉弹层优先
    if (open_combo && open_combo->open) {
        RECT pr = open_combo->popup_rect();
        if (in_rect(pr, p)) {
            const int itemH = 26;
            int idx = (p.y - pr.top - 3) / itemH;
            if (idx >= 0 && idx < (int)open_combo->items.size()) {
                open_combo->index = idx;
            }
            open_combo->open = false;
            RECT inv = pr;
            union_rect_(&inv, open_combo->rc);
            open_combo = nullptr;
            invalidate_ctl(hwnd, inv);
            return;
        }
        Combo* c = open_combo;
        c->open = false;
        open_combo = nullptr;
        RECT inv = c->popup_rect();
        union_rect_(&inv, c->rc);
        invalidate_ctl(hwnd, inv);
    }

    // 侧栏
    if (p.x < U::SidebarW) {
        if (in_rect(theme_rect(), p)) {
            int nx = (theme_index() + 1) % kThemeCount;
            set_theme(nx);
            ui_gdi().build();
            cfg.ui.theme = nx;
            cfg.save_to_file("config.json");
            push_log(std::string("主题已切换: ") + w2u(std::wstring(theme().name)));
            invalidate_all_();
            return;
        }
        int m = menu_hit(p);
        if (m >= 0) { toggle(m); return; }
        return;
    }

    // 面板
    Panel* pn = panel_at(p);
    if (!pn) return;
    raise(pn->kind);

    // 关闭
    if (in_rect(close_rect(*pn), p)) { toggle(pn->kind); return; }

    // 缩放手柄
    if (in_rect(grip_rect(*pn), p)) {
        layout_custom = true;
        drag_mode = 2; drag_kind = pn->kind;
        drag_origin = p;
        ClientToScreen(hwnd, &drag_origin);
        drag_rect0 = pn->rc;
        return;
    }

    // 标题栏 → 移动
    if (in_rect(title_rect(*pn), p)) {
        layout_custom = true;
        drag_mode = 1; drag_kind = pn->kind;
        drag_origin = p;
        ClientToScreen(hwnd, &drag_origin);
        drag_rect0 = pn->rc;
        return;
    }

    // 控件
    Field* hitField = nullptr;
    for (auto& w : pn->ws) {
        if (!w->visible || !in_rect(w->rc, p)) continue;
        w->on_down(p);
        if (auto* f = dynamic_cast<Field*>(w.get())) {
            hitField = f;
        } else if (auto* c = dynamic_cast<Combo*>(w.get())) {
            open_combo = c->open ? c : nullptr;
            invalidate_ctl(hwnd, c->rc);
            RECT pr = c->popup_rect();
            invalidate_ctl(hwnd, pr);
        }
        break;
    }
    set_focus(hitField);
    if (hitField) sync_ime_pos();
}

void MainWindow::Impl::on_up(POINT p) {
    if (GetCapture() == hwnd) ReleaseCapture();
    if (drag_mode != 0) {
        drag_mode = 0;
        int k = drag_kind;
        drag_kind = -1;
        save_layout();
        if (k >= 0) layout_panel(*panels[k]);
        invalidate_all_();
        return;
    }
    Panel* pn = panel_at(p);
    if (!pn) return;
    for (auto& w : pn->ws) {
        if (!w->visible) continue;
        if (w->hover || in_rect(w->rc, p)) {
            w->on_up(p);
            invalidate_ctl(hwnd, w->rc);
        }
    }
}

void MainWindow::Impl::on_wheel(int delta, POINT p) {
    Panel* pn = panel_at(p);
    if (!pn) return;
    for (auto& w : pn->ws) {
        if (w->visible && in_rect(w->rc, p)) {
            w->on_wheel(delta);
            invalidate_ctl(hwnd, w->rc);
            return;
        }
    }
}

bool MainWindow::Impl::on_keydown(int vk, bool ctrl) {
    if (!focus_field) return false;
    if (focus_field->on_key(vk, ctrl)) {
        invalidate_ctl(hwnd, focus_field->rc);
        sync_ime_pos();
        return true;
    }
    return false;
}

bool MainWindow::Impl::on_char(wchar_t c) {
    if (!focus_field) return false;
    if (focus_field->on_char(c)) {
        invalidate_ctl(hwnd, focus_field->rc);
        sync_ime_pos();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 配置 <-> 界面
// ---------------------------------------------------------------------------
static void split_list(const std::wstring& s, std::vector<std::string>& out) {
    out.clear();
    std::wstringstream ss(s);
    std::wstring item;
    while (std::getline(ss, item, L',')) {
        size_t b = item.find_first_not_of(L" \t\r\n");
        size_t e = item.find_last_not_of(L" \t\r\n");
        if (b != std::wstring::npos) out.push_back(w2u(item.substr(b, e - b + 1)));
    }
}

static std::wstring join_list(const std::vector<std::string>& v) {
    std::wstring s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += L", ";
        s += u2w(v[i]);
    }
    return s;
}

void MainWindow::Impl::sync_ui_from_cfg() {
    if (f_base_url)  f_base_url->text = u2w(cfg.ai.base_url);
    if (f_api_key)   f_api_key->text = u2w(cfg.ai.api_key);
    if (f_system)    f_system->text = u2w(cfg.ai.system_prompt);
    if (f_max_tokens) f_max_tokens->text = std::to_wstring(cfg.ai.max_tokens);
    if (f_rounds)    f_rounds->text = std::to_wstring(cfg.ai.max_history_rounds);
    if (c_model) {
        std::wstring want = u2w(cfg.ai.model);
        int idx = -1;
        for (int i = 0; i < (int)c_model->items.size(); ++i) {
            if (c_model->items[i] == want) { idx = i; break; }
        }
        if (idx < 0 && !want.empty()) {
            c_model->items.push_back(want);
            idx = (int)c_model->items.size() - 1;
        }
        c_model->index = (idx < 0) ? 0 : idx;
    }
    if (f_ws_url)    f_ws_url->text = u2w(cfg.onebot.ws_url);
    if (f_bot_qq)    f_bot_qq->text = std::to_wstring(cfg.onebot.bot_qq);
    if (f_token)     f_token->text = u2w(cfg.onebot.access_token);
    if (f_prefixes)  f_prefixes->text = join_list(cfg.onebot.trigger_prefixes);
    if (ck_require_at) ck_require_at->checked = cfg.onebot.group_require_at;
    if (f_at_exempt) f_at_exempt->text = join_list(cfg.onebot.at_exempt_plugins);
    app.model_name = u2w(cfg.ai.model);
}

void MainWindow::Impl::sync_cfg_from_ui() {
    if (f_base_url) cfg.ai.base_url = w2u(f_base_url->text);
    if (f_api_key)  cfg.ai.api_key = w2u(f_api_key->text);
    if (f_system)   cfg.ai.system_prompt = w2u(f_system->text);
    if (f_max_tokens) {
        try { cfg.ai.max_tokens = std::stoi(w2u(f_max_tokens->text)); } catch (...) {}
    }
    if (f_rounds) {
        try { cfg.ai.max_history_rounds = std::stoi(w2u(f_rounds->text)); } catch (...) {}
    }
    if (c_model && c_model->index >= 0 && c_model->index < (int)c_model->items.size()) {
        cfg.ai.model = w2u(c_model->items[c_model->index]);
        app.model_name = c_model->items[c_model->index];
    }
    if (f_ws_url)   cfg.onebot.ws_url = w2u(f_ws_url->text);
    if (f_bot_qq) {
        try { cfg.onebot.bot_qq = std::stoll(w2u(f_bot_qq->text)); } catch (...) {}
    }
    if (f_token)    cfg.onebot.access_token = w2u(f_token->text);
    if (f_prefixes) split_list(f_prefixes->text, cfg.onebot.trigger_prefixes);
    if (ck_require_at) cfg.onebot.group_require_at = ck_require_at->checked;
    if (f_at_exempt) split_list(f_at_exempt->text, cfg.onebot.at_exempt_plugins);
}

void MainWindow::Impl::save_layout() {
    cfg.ui.modules.clear();
    for (int i = 0; i < P_COUNT; ++i) {
        UILayoutItem it;
        it.x = panels[i]->rc.left;
        it.y = panels[i]->rc.top;
        it.w = RW(panels[i]->rc);
        it.h = RH(panels[i]->rc);
        it.visible = panels[i]->open;
        cfg.ui.modules.push_back(it);
    }
    cfg.ui.theme = theme_index();
    cfg.save_to_file("config.json");
}

void MainWindow::Impl::load_layout() {
    bool ok = (cfg.ui.modules.size() == (size_t)P_COUNT);
    if (!ok) { apply_default_layout(); layout_custom = false; return; }
    layout_custom = true;
    for (int i = 0; i < P_COUNT; ++i) {
        const UILayoutItem& it = cfg.ui.modules[i];
        if (it.w > 120 && it.h > 80) {
            panels[i]->rc = RECT{ it.x, it.y, it.x + it.w, it.y + it.h };
        }
        panels[i]->open = it.visible;
        zorder[i] = i;
    }
    for (int i = 0; i < P_COUNT; ++i) {
        if (RW(panels[i]->rc) < U::MinPanelW || RH(panels[i]->rc) < U::MinPanelH) {
            apply_default_layout();
            return;
        }
        clamp_panel(*panels[i]);
    }
}

// ---------------------------------------------------------------------------
// 业务动作
// ---------------------------------------------------------------------------
void MainWindow::Impl::push_log(const std::string& s) {
    {
        std::lock_guard<std::mutex> lk(log_mtx);
        pending.push_back(s);
    }
    if (hwnd) PostMessageW(hwnd, WM_APP_LOG, 0, 0);
}

void MainWindow::Impl::pump_logs() {
    std::vector<std::string> batch;
    {
        std::lock_guard<std::mutex> lk(log_mtx);
        batch.swap(pending);
    }
    if (batch.empty()) return;
    if (batch.size() > 300) batch.erase(batch.begin(), batch.end() - 300);
    if (!logview) return;

    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &tt);
    wchar_t pre[32];
    swprintf_s(pre, L"[%02d:%02d:%02d]  ", tm.tm_hour, tm.tm_min, tm.tm_sec);

    for (auto& m : batch) {
        LogLine ln;
        ln.text = std::wstring(pre) + u2w(m);
        strip_emoji(ln.text);
        ln.level = log_level_of(m);
        logview->lines.push_back(std::move(ln));
        if (m.find("[BotClient] -> 回复成功") != std::string::npos) {
            ++msg_count;
            status_dirty = true;
        }
    }
    const size_t kMax = 2000;
    if (logview->lines.size() > kMax) {
        logview->lines.erase(logview->lines.begin(),
                             logview->lines.begin() + (logview->lines.size() - kMax));
        logview->clamp_top();
    }
    if (logview->follow) logview->scroll_to_end();
    invalidate_ctl(hwnd, logview->rc);
}

void MainWindow::Impl::refresh_plugins() {
    plugins = PluginManager::get_instance().get_plugin_list();
    int enabled = 0;
    for (auto& p : plugins) if (p.enabled) ++enabled;
    if (lb_plugin) {
        lb_plugin->text = L"已加载 " + std::to_wstring(plugins.size()) + L" 个插件，" +
                          std::to_wstring(enabled) + L" 个启用中";
    }
    if (panels.size() > P_PLUGIN) invalidate_ctl(hwnd, panels[P_PLUGIN]->rc);
}

void MainWindow::Impl::toggle_bot() {
    if (bot_running) {
        if (bot) { bot->stop(); bot.reset(); }
        bot_running = false;
        app.bot_running = false;
        if (btn_toggle) btn_toggle->text = L"启动机器人";
        invalidate_all_();
        push_log("机器人已停止");
        return;
    }
    sync_cfg_from_ui();
    if (cfg.ai.api_key.empty()) {
        panels[P_AI]->open = true;
        raise(P_AI);
        save_layout();
        invalidate_all_();
        set_focus(f_api_key);
        MessageBoxW(hwnd, L"请先在「AI 接口配置」面板填写 API Key。",
                    L"缺少配置", MB_OK | MB_ICONWARNING);
        return;
    }
    push_log("正在连接 OneBot: " + cfg.onebot.ws_url + " ...");
    bot = std::make_unique<BotClient>(cfg);
    bot->start();
    bot_running = true;
    app.bot_running = true;
    if (btn_toggle) btn_toggle->text = L"停止机器人";
    invalidate_all_();
    push_log("机器人已上线，正在监听消息");
}

void MainWindow::Impl::save_config() {
    sync_cfg_from_ui();
    save_layout();
    push_log("配置已保存到 config.json");
    MessageBoxW(hwnd, L"配置已保存。", L"保存成功", MB_OK | MB_ICONINFORMATION);
}

struct TestPayload {
    bool ok = false;
    std::string detail;
};

void MainWindow::Impl::test_api() {
    sync_cfg_from_ui();
    push_log("正在测试 AI 接口: " + cfg.ai.base_url + " ...");
    AppConfig snapshot = cfg;
    HWND target = hwnd;
    std::thread([snapshot, target]() {
        TestPayload* tp = new (std::nothrow) TestPayload();
        if (!tp) return;
        try {
            AIClient c(snapshot.ai);
            std::string r = c.chat("test_ping", "请回复两个字：收到");
            tp->ok = (r.find("【AI") == std::string::npos);
            tp->detail = r;
        } catch (const std::exception& e) {
            tp->detail = e.what();
        } catch (...) {
            tp->detail = "unknown error";
        }
        if (target) PostMessageW(target, WM_APP_TESTDONE, 0, (LPARAM)tp);
        else delete tp;
    }).detach();
}

// ---------------------------------------------------------------------------
// 窗口过程
// ---------------------------------------------------------------------------
LRESULT CALLBACK MainWindow::Impl::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Impl* self = (Impl*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        self = (Impl*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        if (self) self->hwnd = hwnd;
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;   // 全部在 WM_PAINT 里双缓冲绘制

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        PB pb;
        if (pb.begin(dc, ps.rcPaint)) {
            self->paint(pb.mem(), ps.rcPaint);
            pb.end(dc);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            int w = LOWORD(lp), h = HIWORD(lp);
            // 还在用默认布局时，跟随窗口重新铺满；用户自定义过就只做边界收敛
            if (!self->layout_custom) self->apply_default_layout();
            else for (auto& p : self->panels) self->clamp_panel(*p);
            self->layout_all();
            self->last_w_ = w;
            self->last_h_ = h;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = 960;
        mm->ptMinTrackSize.y = 660;
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        self->on_move(p);
        // hover 需要持续跟踪鼠标离开
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (self->hover_menu >= 0) {
            InvalidateRect(hwnd, NULL, FALSE);
            self->hover_menu = -1;
        }
        for (auto& p : self->panels)
            for (auto& w : p->ws)
                if (w->hover) { w->hover = false; invalidate_ctl(hwnd, w->rc); }
        return 0;

    case WM_LBUTTONDOWN: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        self->on_down(p);
        return 0;
    }

    case WM_LBUTTONUP: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        self->on_up(p);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &p);
        self->on_wheel(GET_WHEEL_DELTA_WPARAM(wp), p);
        return 0;
    }

    case WM_KEYDOWN: {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (self->on_keydown((int)wp, ctrl)) return 0;
        break;
    }

    case WM_CHAR:
        if (self->on_char((wchar_t)wp)) return 0;
        break;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            if (self->drag_mode == 2) SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
            else if (self->drag_mode == 1) SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            else {
                Panel* pn = self->panel_at(p);
                bool onTitle = pn && in_rect(self->title_rect(*pn), p);
                bool onGrip  = pn && in_rect(self->grip_rect(*pn), p);
                if (onGrip)       SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
                else if (onTitle) SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                else              SetCursor(LoadCursor(NULL, IDC_ARROW));
            }
            return TRUE;
        }
        break;

    case WM_TIMER:
        if (wp == IDT_CARET) {
            if (self->focus_field) invalidate_ctl(hwnd, self->focus_field->rc);
            return 0;
        }
        if (wp == IDT_STATUS) {
            if (self->status_dirty) {
                self->status_dirty = false;
                // 侧栏状态胶囊 + 运行状态面板
                RECT a = { 0, 54, U::SidebarW, 92 };
                invalidate_ctl(hwnd, a);
                invalidate_ctl(hwnd, self->panels[P_STATUS]->rc);
            }
            return 0;
        }
        return 0;

    case WM_APP_LOG:
        self->pump_logs();
        return 0;

    case WM_APP_TESTDONE: {
        TestPayload* tp = (TestPayload*)lp;
        if (tp) {
            if (tp->ok) {
                self->push_log("接口测试通过，模型回复: " + tp->detail);
                MessageBoxW(hwnd, L"接口连通，大模型已就绪。", L"测试成功",
                            MB_OK | MB_ICONINFORMATION);
            } else {
                self->push_log("接口测试失败: " + tp->detail);
                MessageBoxW(hwnd, u2w("接口测试失败：\r\n" + tp->detail).c_str(),
                            L"测试失败", MB_OK | MB_ICONERROR);
            }
            delete tp;
        }
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, IDT_CARET);
        KillTimer(hwnd, IDT_STATUS);
        for (BufSlot& s : g_bufs) {
            if (s.bmp) { DeleteObject(s.bmp); s.bmp = nullptr; }
            if (s.hdc) { DeleteDC(s.hdc); s.hdc = nullptr; }
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------
MainWindow::MainWindow() : d_(std::make_unique<Impl>()) {}
MainWindow::~MainWindow() {
    if (d_ && d_->app.f_brand) {
        HFONT fs[] = { d_->app.f_brand, d_->app.f_h2, d_->app.f_body,
                       d_->app.f_bold, d_->app.f_small, d_->app.f_log };
        for (HFONT f : fs) if (f) DeleteObject(f);
    }
}

MainWindow& MainWindow::get_instance() {
    static MainWindow inst;
    return inst;
}

bool MainWindow::create_and_show(HINSTANCE hInstance, int nCmdShow) {
    Impl& m = *d_;
    m.inst = hInstance;

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    m.cfg = AppConfig::load_from_file("config.json");
    set_theme(m.cfg.ui.theme);
    ui_gdi().build();
    m.make_fonts();
    m.build_panels();
    m.sync_ui_from_cfg();

    WNDCLASSW wc{};
    wc.lpfnWndProc = Impl::proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TabMind_Main4";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = ui_gdi().br_win;
    if (!RegisterClassW(&wc)) return false;

    RECT wa{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = 1200, h = 780;
    int maxW = (wa.right - wa.left) - 24;
    int maxH = (wa.bottom - wa.top) - 24;
    if (maxW < 960) maxW = 960;
    if (maxH < 660) maxH = 660;
    if (w > maxW) w = maxW;
    if (h > maxH) h = maxH;
    int x = wa.left + ((wa.right - wa.left) - w) / 2;
    int y = wa.top + ((wa.bottom - wa.top) - h) / 2;

    HWND hwnd = CreateWindowExW(0, L"TabMind_Main4", L"TabMind · 智能聊天机器人管理终端",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                x, y, w, h, NULL, NULL, hInstance, &m);
    if (!hwnd) return false;

    m.load_layout();
    m.layout_all();
    m.refresh_plugins();

    SetTimer(hwnd, IDT_CARET, 530, NULL);
    SetTimer(hwnd, IDT_STATUS, 400, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    m.push_log("TabMind 已就绪 · 左侧菜单可开关各功能面板");
    m.push_log("OneBot 服务端: " + m.cfg.onebot.ws_url);

    if (!m.cfg.ai.api_key.empty()) {
        m.push_log("正在连接 OneBot WebSocket ...");
        m.bot = std::make_unique<BotClient>(m.cfg);
        m.bot->start();
        m.bot_running = true;
        m.app.bot_running = true;
        if (m.btn_toggle) m.btn_toggle->text = L"停止机器人";
        m.push_log("机器人已上线，正在监听消息");
    } else {
        m.push_log("未配置 API Key，请打开「AI 接口配置」面板填写");
    }
    m.pump_logs();
    return true;
}

void MainWindow::run_message_loop() {
    MSG msg{};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void MainWindow::log(const std::string& message) {
    if (d_) d_->push_log(message);
}


