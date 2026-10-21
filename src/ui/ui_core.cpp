// ===========================================================================
// TabMind 前端 v4 · 控件实现（全部自绘，无子窗口）
// ===========================================================================
#include "ui_core.hpp"
#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// 基础工具
// ---------------------------------------------------------------------------
std::wstring u2w(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
std::string w2u(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

static bool pt_in(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}
static bool pt_in(const RECT& r, POINT p) { return pt_in(r, p.x, p.y); }
static int  rw(const RECT& r) { return r.right - r.left; }
static int  rh(const RECT& r) { return r.bottom - r.top; }

int log_level_of(const std::string& raw) {
    auto has = [&](const char* k) { return raw.find(k) != std::string::npos; };
    if (has("\xE2\x9D\x8C")) return 4;                 // U+274C
    if (has("\xE2\x9A\xA0")) return 3;                 // U+26A0
    if (has("\xE2\x9C\x85")) return 2;                 // U+2705
    if (has("失败") || has("错误") || has("异常") || has("FAIL")) return 4;
    if (has("未配置") || has("警告")) return 3;
    if (has("已上线") || has("已就绪") || has("通过")) return 2;
    if (has("AI") || has("回复") || has("模型")) return 1;
    return 0;
}

// ---------------------------------------------------------------------------
// 绘制助手
// ---------------------------------------------------------------------------
namespace draw {

void text(HDC dc, HFONT f, COLORREF c, const RECT& rc, const std::wstring& s, UINT flags) {
    HGDIOBJ old = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    RECT r = rc;
    DrawTextW(dc, s.c_str(), -1, &r, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}

void round_rect(HDC dc, const RECT& rc, int r, HBRUSH br, HPEN pen) {
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, r, r);
    SelectObject(dc, op);
    SelectObject(dc, ob);
}

void pill(HDC dc, const RECT& rc, HBRUSH br, COLORREF fg,
          const std::wstring& s, HFONT f) {
    int r = rh(rc) / 2;
    HGDIOBJ op = SelectObject(dc, ui_gdi().pen_null);
    round_rect(dc, rc, r, br, ui_gdi().pen_null);
    SelectObject(dc, op);
    text(dc, f, fg, rc, s, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void card(HDC dc, const RECT& rc, int r) {
    const ThemePalette& t = theme();
    UiGdiCache& g = ui_gdi();
    // 阴影：往下偏移几像素的圆角块（在卡片背后先画）
    for (int i = 2; i >= 1; --i) {
        RECT sh = { rc.left + 1, rc.top + i, rc.right - 1, rc.bottom + i };
        round_rect(dc, sh, r + 1, g.br_shadow, g.pen_null);
    }
    round_rect(dc, rc, r, g.br_card, g.pen_border);
    (void)t;
}

} // namespace draw

// ---------------------------------------------------------------------------
// Label
// ---------------------------------------------------------------------------
void Label::paint(HDC dc, App& app) {
    const ThemePalette& t = theme();
    COLORREF c = (color == CLR_INVALID) ? t.text_sub : color;
    draw::text(dc, font ? font : app.f_small, c, rc, text,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------
void Button::paint(HDC dc, App& app) {
    const ThemePalette& t = theme();
    UiGdiCache& g = ui_gdi();
    bool down = hover && (GetKeyState(VK_LBUTTON) & 0x8000);

    COLORREF fg = t.text_main;
    if (primary) {
        COLORREF c = danger ? t.danger : t.primary;
        if (down)        c = danger ? RGB(185, 28, 28) : t.primary_dark;
        else if (hover)  c = danger ? RGB(220, 38, 38) : t.primary_dark;
        HBRUSH hb = CreateSolidBrush(c);
        draw::round_rect(dc, rc, 8, hb, g.pen_null);
        DeleteObject(hb);
        fg = RGB(255, 255, 255);
    } else {
        HBRUSH bg = g.br_card;
        HPEN   bd = g.pen_border;
        if (down)       bg = g.br_border;
        else if (hover) { bg = g.br_field; bd = g.pen_text_sub; }
        draw::round_rect(dc, rc, 8, bg, bd);
    }
    draw::text(dc, font ? font : app.f_bold, fg, rc, text,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void Button::on_down(POINT) { hover = true; }
void Button::on_up(POINT p) {
    if (pt_in(rc, p) && click) click();
}

// ---------------------------------------------------------------------------
// Field
// ---------------------------------------------------------------------------
std::wstring Field::shown() const {
    if (!password) return text;
    return std::wstring(text.size(), L'\u2022');
}

int Field::measure(HDC dc, const std::wstring& s) {
    if (s.empty()) return 0;
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    return sz.cx;
}

std::vector<std::wstring> Field::split_lines(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == L'\r') continue;
        if (c == L'\n') { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

int Field::px_of_index(HDC dc, const std::wstring& s, int idx) const {
    if (idx <= 0) return 0;
    if (idx > (int)s.size()) idx = (int)s.size();
    return measure(dc, s.substr(0, idx));
}

int Field::index_of_px(HDC dc, const std::wstring& s, int px) const {
    if (px <= 0) return 0;
    for (int i = 1; i <= (int)s.size(); ++i) {
        if (px_of_index(dc, s, i) > px) return i - 1;
    }
    return (int)s.size();
}

void Field::paint(HDC dc, App& app) {
    const ThemePalette& t = theme();
    UiGdiCache& g = ui_gdi();
    HPEN bd = focused ? g.pen_primary : g.pen_border;
    draw::round_rect(dc, rc, 7, g.br_field, bd);

    RECT inner = rc;
    inner.left += 9;
    inner.right -= 9;
    if (multiline) { inner.top += 6; inner.bottom -= 6; }

    std::wstring vis = shown();
    HGDIOBJ oldFont = SelectObject(dc, app.f_body);
    SetBkMode(dc, TRANSPARENT);

    if (!multiline) {
        if (vis.empty() && !focused && !placeholder.empty()) {
            draw::text(dc, app.f_body, t.text_faint, inner, placeholder,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, oldFont);
            return;
        }
        int caretPx = px_of_index(dc, vis, caret);
        int availW = rw(inner);
        if (focused) {
            if (caretPx - scroll > availW - 2) scroll = caretPx - availW + 2;
            if (caretPx - scroll < 0) scroll = caretPx;
        } else {
            scroll = 0;
        }
        if (scroll < 0) scroll = 0;

        // 用 IntersectClipRect（逻辑坐标 + 求交）而不是 SelectClipRgn：
        // 双缓冲平移了逻辑原点，region 是设备坐标，选进去会让裁剪整体错位
        int sv = SaveDC(dc);
        IntersectClipRect(dc, inner.left, inner.top, inner.right, inner.bottom);

        if (focused && anchor >= 0 && anchor != caret) {
            int a = (std::min)(anchor, caret), b = (std::max)(anchor, caret);
            int x1 = inner.left + px_of_index(dc, vis, a) - scroll;
            int x2 = inner.left + px_of_index(dc, vis, b) - scroll;
            RECT sr = { x1, inner.top + 4, x2, inner.bottom - 4 };
            if (sr.right > sr.left) {
                HBRUSH hb = CreateSolidBrush(t.primary_soft);
                FillRect(dc, &sr, hb);
                DeleteObject(hb);
            }
        }

        draw::text(dc, app.f_body, t.text_main, RECT{ inner.left - scroll, inner.top,
                                                      inner.right - scroll, inner.bottom },
                   vis, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (focused) {
            int cx = inner.left + caretPx - scroll;
            HPEN pen = CreatePen(PS_SOLID, 1, t.primary);
            HGDIOBJ op = SelectObject(dc, pen);
            MoveToEx(dc, cx, inner.top + 5, NULL);
            LineTo(dc, cx, inner.bottom - 5);
            SelectObject(dc, op);
            DeleteObject(pen);
        }
        RestoreDC(dc, sv);
    } else {
        auto lines = split_lines(vis);
        const int lineH = 19;
        int first = scroll;
        if (first < 0) first = 0;
        if (first >= (int)lines.size()) first = (int)lines.size() - 1;
        if (first < 0) first = 0;

        int sv2 = SaveDC(dc);
        IntersectClipRect(dc, inner.left, inner.top, inner.right, inner.bottom);

        int y = inner.top;
        for (int i = first; i < (int)lines.size(); ++i) {
            if (y >= inner.bottom) break;
            draw::text(dc, app.f_body, t.text_main,
                       RECT{ inner.left, y, inner.right, y + lineH },
                       lines[i], DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += lineH;
        }

        if (focused) {
            int lineIdx = 0, acc = 0;
            for (int i = 0; i < (int)vis.size() && i < caret; ++i) {
                if (vis[i] == L'\n') { ++lineIdx; acc = i + 1; }
            }
            int col = caret - acc;
            int cx = inner.left + px_of_index(dc, lines[lineIdx], col);
            int cy = inner.top + (lineIdx - first) * lineH;
            if (cy >= inner.top - lineH && cy <= inner.bottom) {
                HPEN pen = CreatePen(PS_SOLID, 1, t.primary);
                HGDIOBJ op = SelectObject(dc, pen);
                MoveToEx(dc, cx, cy + 3, NULL);
                LineTo(dc, cx, cy + lineH - 2);
                SelectObject(dc, op);
                DeleteObject(pen);
            }
        }
        RestoreDC(dc, sv2);
    }
    SelectObject(dc, oldFont);
}

void Field::on_down(POINT p) {
    // 用屏幕 DC 量文字宽度（与绘制时同一字体近似）
    HDC dc = GetDC(NULL);
    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HGDIOBJ old = SelectObject(dc, f);
    std::wstring vis = shown();
    int innerL = rc.left + 9;

    if (!multiline) {
        caret = index_of_px(dc, vis, p.x - innerL + scroll);
    } else {
        const int lineH = 19;
        int lineIdx = (p.y - (rc.top + 6)) / lineH + scroll;
        auto lines = split_lines(vis);
        if (lineIdx < 0) lineIdx = 0;
        if (lineIdx >= (int)lines.size()) lineIdx = (int)lines.size() - 1;
        int col = index_of_px(dc, lines[lineIdx], p.x - innerL);
        int acc = 0;
        for (int i = 0; i < lineIdx; ++i) acc += (int)lines[i].size() + 1;
        caret = acc + col;
    }
    if (caret > (int)text.size()) caret = (int)text.size();
    if (caret < 0) caret = 0;
    anchor = caret;

    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
}

bool Field::on_char(wchar_t c) {
    auto erase_sel = [&]() -> bool {
        if (anchor >= 0 && anchor != caret) {
            int a = (std::min)(anchor, caret), b = (std::max)(anchor, caret);
            text.erase(a, b - a);
            caret = a;
            anchor = -1;
            return true;
        }
        return false;
    };

    if (c == L'\b') {
        if (erase_sel()) return true;
        if (caret > 0) { text.erase(caret - 1, 1); --caret; }
        return true;
    }
    if (c == L'\r' || c == L'\n') {
        if (!multiline) return false;
        erase_sel();
        text.insert(text.begin() + caret, L'\n');
        ++caret;
        return true;
    }
    if (c < 32) return false;
    erase_sel();
    text.insert(text.begin() + caret, c);
    ++caret;
    return true;
}

bool Field::on_key(int vk, bool ctrl) {
    auto erase_sel = [&]() -> bool {
        if (anchor >= 0 && anchor != caret) {
            int a = (std::min)(anchor, caret), b = (std::max)(anchor, caret);
            text.erase(a, b - a);
            caret = a;
            anchor = -1;
            return true;
        }
        return false;
    };

    if (ctrl) {
        if (vk == 'A') { anchor = 0; caret = (int)text.size(); return true; }
        if (vk == 'C' || vk == 'X') {
            if (anchor >= 0 && anchor != caret && OpenClipboard(NULL)) {
                int a = (std::min)(anchor, caret), b = (std::max)(anchor, caret);
                std::wstring sel = text.substr(a, b - a);
                EmptyClipboard();
                HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (sel.size() + 1) * sizeof(wchar_t));
                if (h) {
                    wchar_t* q = (wchar_t*)GlobalLock(h);
                    memcpy(q, sel.c_str(), (sel.size() + 1) * sizeof(wchar_t));
                    GlobalUnlock(h);
                    SetClipboardData(CF_UNICODETEXT, h);
                }
                CloseClipboard();
                if (vk == 'X') { text.erase(a, b - a); caret = a; anchor = -1; }
            }
            return true;
        }
        if (vk == 'V') {
            if (OpenClipboard(NULL)) {
                HANDLE h = GetClipboardData(CF_UNICODETEXT);
                if (h) {
                    wchar_t* q = (wchar_t*)GlobalLock(h);
                    if (q) {
                        erase_sel();
                        std::wstring add = q;
                        for (auto& ch : add) if (ch == L'\r') ch = L' ';
                        if (!multiline) add.erase(std::remove(add.begin(), add.end(), L'\n'), add.end());
                        text.insert(caret, add);
                        caret += (int)add.size();
                    }
                    GlobalUnlock(h);
                }
                CloseClipboard();
            }
            return true;
        }
        return false;
    }

    switch (vk) {
    case VK_LEFT:  if (caret > 0) --caret; anchor = -1; return true;
    case VK_RIGHT: if (caret < (int)text.size()) ++caret; anchor = -1; return true;
    case VK_HOME:  caret = 0; anchor = -1; return true;
    case VK_END:   caret = (int)text.size(); anchor = -1; return true;
    case VK_DELETE:
        if (!erase_sel() && caret < (int)text.size()) text.erase(caret, 1);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Combo
// ---------------------------------------------------------------------------
void Combo::paint(HDC dc, App& app) {
    const ThemePalette& t = theme();
    UiGdiCache& g = ui_gdi();
    draw::round_rect(dc, rc, 7, g.br_field, focused ? g.pen_primary : g.pen_border);

    std::wstring cur = (index >= 0 && index < (int)items.size()) ? items[index] : L"";
    draw::text(dc, app.f_body, t.text_main,
               RECT{ rc.left + 9, rc.top, rc.right - 24, rc.bottom },
               cur, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    HGDIOBJ op = SelectObject(dc, g.pen_text_sub);
    int cx = rc.right - 13, cy = (rc.top + rc.bottom) / 2;
    MoveToEx(dc, cx - 4, cy - 2, NULL);
    LineTo(dc, cx, cy + 2);
    LineTo(dc, cx + 4, cy - 2);
    SelectObject(dc, op);
}

RECT Combo::popup_rect() const {
    int itemH = 26;
    int n = (int)items.size();
    if (n > 8) n = 8;
    RECT r = { rc.left, rc.bottom + 2, rc.right, rc.bottom + 2 + n * itemH + 6 };
    return r;
}

void Combo::paint_popup(HDC dc, App& app) {
    if (!open) return;
    const ThemePalette& t = theme();
    UiGdiCache& g = ui_gdi();
    RECT pr = popup_rect();
    draw::round_rect(dc, pr, 8, g.br_card, g.pen_border);
    const int itemH = 26;
    int y = pr.top + 3;
    for (int i = 0; i < (int)items.size() && i < 8; ++i) {
        RECT ir = { pr.left + 3, y, pr.right - 3, y + itemH };
        bool sel = (i == index);
        if (sel) {
            draw::round_rect(dc, ir, 6, g.br_primary_soft, g.pen_null);
        }
        draw::text(dc, app.f_body, sel ? t.primary : t.text_main,
                   RECT{ ir.left + 8, ir.top, ir.right - 8, ir.bottom },
                   items[i], DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += itemH;
    }
}

void Combo::on_down(POINT) { open = !open; }

// ---------------------------------------------------------------------------
// Check
// ---------------------------------------------------------------------------
void Check::paint(HDC dc, App& app) {
    const ThemePalette& t = theme();
    UiGdiCache& g = ui_gdi();
    const int box = 16;
    RECT br = { rc.left, rc.top + (rh(rc) - box) / 2,
                rc.left + box, rc.top + (rh(rc) - box) / 2 + box };
    if (checked) {
        draw::round_rect(dc, br, 4, g.br_primary, g.pen_null);
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HGDIOBJ op = SelectObject(dc, pen);
        MoveToEx(dc, br.left + 4, br.top + 8, NULL);
        LineTo(dc, br.left + 7, br.top + 11);
        LineTo(dc, br.right - 3, br.top + 4);
        SelectObject(dc, op);
        DeleteObject(pen);
    } else {
        draw::round_rect(dc, br, 4, g.br_field, hover ? g.pen_text_sub : g.pen_border);
    }
    draw::text(dc, app.f_body, t.text_main,
               RECT{ br.right + 8, rc.top, rc.right, rc.bottom },
               text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void Check::on_up(POINT p) {
    if (pt_in(rc, p)) {
        checked = !checked;
        if (on_change) on_change();
    }
}

// ---------------------------------------------------------------------------
// LogView
// ---------------------------------------------------------------------------
int LogView::rows_per_page() const {
    int h = rh(rc) - 12;
    int n = h / U::LogRowH;
    return n < 1 ? 1 : n;
}

void LogView::clamp_top() {
    int maxTop = (std::max)(0, (int)lines.size() - rows_per_page());
    if (top > maxTop) top = maxTop;
    if (top < 0) top = 0;
}

void LogView::scroll_to_end() {
    top = (std::max)(0, (int)lines.size() - rows_per_page());
    follow = true;
}

void LogView::paint(HDC dc, App& app) {
    const ThemePalette& t = theme();
    UiGdiCache& g = ui_gdi();
    draw::round_rect(dc, rc, 7, g.br_log, g.pen_border);

    RECT inner = { rc.left + 8, rc.top + 6, rc.right - 10, rc.bottom - 6 };
    int sv = SaveDC(dc);
    IntersectClipRect(dc, inner.left, inner.top, inner.right, inner.bottom);

    int perPage = rows_per_page();
    int y = inner.top;
    for (int i = 0; i < perPage; ++i) {
        int idx = top + i;
        if (idx >= (int)lines.size()) break;
        COLORREF c = t.log_info;
        switch (lines[idx].level) {
        case 4: c = t.log_err; break;
        case 3: c = t.log_warn; break;
        case 2: c = t.log_ok; break;
        case 1: c = t.log_ai; break;
        }
        draw::text(dc, app.f_log, c,
                   RECT{ inner.left, y, inner.right, y + U::LogRowH },
                   lines[idx].text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += U::LogRowH;
    }

    int total = (int)lines.size();
    if (total > perPage) {
        int trackH = rh(inner);
        int thumbH = (std::max)(24, trackH * perPage / total);
        int maxTop = total - perPage;
        int y0 = inner.top + (maxTop > 0 ? (trackH - thumbH) * top / maxTop : 0);
        RECT tr = { rc.right - 7, y0, rc.right - 4, y0 + thumbH };
        draw::round_rect(dc, tr, 2, g.br_text_faint, g.pen_null);
    }

    RestoreDC(dc, sv);
}

void LogView::on_wheel(int delta) {
    top += (delta > 0) ? -3 : 3;
    clamp_top();
    follow = (top + rows_per_page() >= (int)lines.size());
}
