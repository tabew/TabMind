#pragma once
#include <windows.h>
#include <string>

// ---------------------------------------------------------------------------
// 主题调色板：所有颜色集中在这里，界面绘制统一取 theme()。
// 新增一套主题只需在 kThemes 里加一项，无需改任何绘制代码。
// ---------------------------------------------------------------------------
struct ThemePalette {
    const wchar_t* name;
    bool dark;                  // 深色系：文字/日志取色不同

    COLORREF win_bg;
    COLORREF header_bg;
    COLORREF card_bg;
    COLORREF border;
    COLORREF border_soft;
    COLORREF field_bg;
    COLORREF shadow;

    COLORREF text_main;
    COLORREF text_sub;
    COLORREF text_faint;

    COLORREF primary;
    COLORREF primary_dark;
    COLORREF primary_soft;

    COLORREF success;
    COLORREF success_soft;
    COLORREF success_text;
    COLORREF warn;
    COLORREF danger;
    COLORREF danger_soft;

    COLORREF log_bg;
    COLORREF log_text;
    COLORREF log_time;
    COLORREF log_info;
    COLORREF log_ok;
    COLORREF log_warn;
    COLORREF log_err;
    COLORREF log_ai;
};

inline const ThemePalette kThemes[] = {
    // 0 · 明亮（默认）
    {
        L"明亮", false,
        RGB(247, 249, 252), RGB(255, 255, 255), RGB(255, 255, 255),
        RGB(226, 232, 240), RGB(237, 242, 247), RGB(244, 247, 250), RGB(241, 244, 249),
        RGB(15, 23, 42), RGB(100, 116, 139), RGB(148, 163, 184),
        RGB(79, 70, 229), RGB(67, 56, 202), RGB(238, 242, 255),
        RGB(16, 185, 129), RGB(209, 250, 229), RGB(5, 150, 105),
        RGB(245, 158, 11), RGB(239, 68, 68), RGB(254, 226, 226),
        RGB(11, 18, 32), RGB(226, 232, 240), RGB(100, 116, 139),
        RGB(203, 213, 225), RGB(52, 211, 153), RGB(251, 191, 36), RGB(248, 113, 113), RGB(147, 197, 253)
    },
    // 1 · 暗夜
    {
        L"暗夜", true,
        RGB(14, 18, 24), RGB(20, 26, 34), RGB(23, 30, 39),
        RGB(37, 48, 64), RGB(28, 37, 49), RGB(29, 38, 50), RGB(10, 14, 19),
        RGB(231, 238, 248), RGB(147, 164, 184), RGB(107, 124, 145),
        RGB(124, 140, 255), RGB(94, 108, 235), RGB(30, 39, 64),
        RGB(52, 211, 153), RGB(16, 49, 37), RGB(110, 231, 183),
        RGB(251, 191, 36), RGB(248, 113, 113), RGB(58, 29, 29),
        RGB(10, 14, 19), RGB(216, 226, 239), RGB(93, 107, 125),
        RGB(195, 206, 218), RGB(74, 222, 128), RGB(252, 211, 77), RGB(251, 113, 133), RGB(147, 197, 253)
    },
    // 2 · 北境（Nord）
    {
        L"北境", false,
        RGB(237, 240, 245), RGB(255, 255, 255), RGB(255, 255, 255),
        RGB(216, 222, 233), RGB(229, 233, 240), RGB(243, 245, 249), RGB(226, 231, 238),
        RGB(46, 52, 64), RGB(76, 86, 106), RGB(123, 134, 152),
        RGB(94, 129, 172), RGB(76, 111, 148), RGB(229, 237, 247),
        RGB(163, 190, 140), RGB(234, 241, 228), RGB(108, 138, 85),
        RGB(235, 203, 139), RGB(191, 97, 106), RGB(247, 228, 230),
        RGB(46, 52, 64), RGB(236, 239, 244), RGB(123, 134, 152),
        RGB(216, 222, 233), RGB(163, 190, 140), RGB(235, 203, 139), RGB(191, 97, 106), RGB(136, 192, 208)
    },
    // 3 · 暖沙
    {
        L"暖沙", false,
        RGB(250, 246, 240), RGB(255, 255, 255), RGB(255, 253, 249),
        RGB(234, 223, 208), RGB(241, 233, 220), RGB(246, 239, 228), RGB(240, 232, 219),
        RGB(58, 46, 34), RGB(122, 106, 87), RGB(167, 151, 129),
        RGB(194, 112, 61), RGB(166, 92, 46), RGB(251, 238, 228),
        RGB(108, 138, 85), RGB(234, 241, 228), RGB(85, 112, 63),
        RGB(217, 160, 60), RGB(192, 80, 63), RGB(248, 229, 225),
        RGB(42, 33, 24), RGB(242, 233, 220), RGB(156, 138, 115),
        RGB(228, 216, 198), RGB(163, 190, 140), RGB(232, 192, 122), RGB(224, 137, 116), RGB(201, 177, 138)
    },
};

inline const int kThemeCount = (int)(sizeof(kThemes) / sizeof(kThemes[0]));
inline int& theme_index_ref() { static int idx = 0; return idx; }

inline int theme_index() { return theme_index_ref(); }
inline void set_theme(int idx) {
    if (idx < 0) idx = 0;
    if (idx >= kThemeCount) idx = kThemeCount - 1;
    theme_index_ref() = idx;
}
inline const ThemePalette& theme() { return kThemes[theme_index_ref()]; }

// ---------------------------------------------------------------------------
// 通用绘制助手（全部按当前主题取色）
// ---------------------------------------------------------------------------
inline void ui_fill_round(HDC hdc, const RECT& rc, int radius, COLORREF fill,
                          COLORREF border = CLR_INVALID, int bw = 1) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = (border == CLR_INVALID) ? CreatePen(PS_NULL, 0, 0)
                                       : CreatePen(PS_SOLID, bw, border);
    HGDIOBJ ob = SelectObject(hdc, br);
    HGDIOBJ op = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

// 卡片：柔和投影 + 圆角底 + 描边
inline void ui_draw_card(HDC hdc, const RECT& rc, int radius = 12) {
    const ThemePalette& t = theme();
    for (int i = 3; i >= 1; --i) {
        RECT sh = { rc.left - i / 2, rc.top + i, rc.right + i / 2, rc.bottom + i };
        ui_fill_round(hdc, sh, radius + i, t.shadow);
    }
    ui_fill_round(hdc, rc, radius, t.card_bg, t.border);
}

// ---------------------------------------------------------------------------
// GDI 对象缓存
// 之前每画一次圆角/一次色块就 CreateSolidBrush + CreatePen，画完再 Delete，
// 一帧下来几十次内核对象创建销毁，是卡顿的直接来源之一。
// 这里按主题一次性建好，绘制路径只做 SelectObject。
// ---------------------------------------------------------------------------
struct UiGdiCache {
    HBRUSH br_win = nullptr, br_card = nullptr, br_field = nullptr, br_log = nullptr;
    HBRUSH br_shadow = nullptr, br_border = nullptr, br_text_faint = nullptr;
    HBRUSH br_primary = nullptr, br_primary_soft = nullptr;
    HBRUSH br_success = nullptr, br_success_soft = nullptr;
    HBRUSH br_danger = nullptr, br_danger_soft = nullptr;
    HPEN pen_border = nullptr, pen_border_soft = nullptr, pen_primary = nullptr;
    HPEN pen_text_sub = nullptr, pen_null = nullptr;

    void clear() {
        HBRUSH brs[] = { br_win, br_card, br_field, br_log, br_shadow, br_border, br_text_faint,
                         br_primary, br_primary_soft, br_success, br_success_soft,
                         br_danger, br_danger_soft };
        for (HBRUSH b : brs) if (b) DeleteObject(b);
        HPEN pens[] = { pen_border, pen_border_soft, pen_primary, pen_text_sub, pen_null };
        for (HPEN p : pens) if (p) DeleteObject(p);
        *this = UiGdiCache{};
    }

    void build() {
        clear();
        const ThemePalette& t = theme();
        br_win          = CreateSolidBrush(t.win_bg);
        br_card         = CreateSolidBrush(t.card_bg);
        br_field        = CreateSolidBrush(t.field_bg);
        br_log          = CreateSolidBrush(t.log_bg);
        br_shadow       = CreateSolidBrush(t.shadow);
        br_border       = CreateSolidBrush(t.border);
        br_text_faint   = CreateSolidBrush(t.text_faint);
        br_primary      = CreateSolidBrush(t.primary);
        br_primary_soft = CreateSolidBrush(t.primary_soft);
        br_success      = CreateSolidBrush(t.success);
        br_success_soft = CreateSolidBrush(t.success_soft);
        br_danger       = CreateSolidBrush(t.danger);
        br_danger_soft  = CreateSolidBrush(t.danger_soft);
        pen_border      = CreatePen(PS_SOLID, 1, t.border);
        pen_border_soft = CreatePen(PS_SOLID, 1, t.border_soft);
        pen_primary     = CreatePen(PS_SOLID, 1, t.primary);
        pen_text_sub    = CreatePen(PS_SOLID, 2, t.text_sub);
        pen_null        = CreatePen(PS_NULL, 0, 0);
    }
};

inline UiGdiCache& ui_gdi() { static UiGdiCache cache; return cache; }

// 用缓存对象画圆角矩形（绘制热路径专用）
inline void ui_round(HDC hdc, const RECT& rc, int rx, int ry, HBRUSH br, HPEN pen) {
    HGDIOBJ ob = SelectObject(hdc, br);
    HGDIOBJ op = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, rx, ry);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
}

inline void ui_round_ltwh(HDC hdc, int l, int t, int r, int b, int rr, HBRUSH br, HPEN pen) {
    RECT rc = { l, t, r, b };
    ui_round(hdc, rc, rr, rr, br, pen);
}

inline void ui_pill(HDC hdc, const RECT& rc, HBRUSH br, HPEN pen, COLORREF fg,
                    const std::wstring& text, HFONT font) {
    int rr = (rc.bottom - rc.top) / 2;
    ui_round(hdc, rc, rr, rr, br, pen);
    HGDIOBJ old = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);
    RECT tr = rc;
    DrawTextW(hdc, text.c_str(), -1, &tr,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, old);
}

inline void ui_draw_pill(HDC hdc, const RECT& rc, COLORREF bg, COLORREF fg,
                         const std::wstring& text, HFONT font) {
    ui_fill_round(hdc, rc, rc.bottom - rc.top, bg);
    HGDIOBJ old = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);
    RECT tr = rc;
    DrawTextW(hdc, text.c_str(), -1, &tr,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, old);
}
