#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "config.hpp"
#include "ui_theme.hpp"

// ---------------------------------------------------------------------------
// TabMind 前端 v4 · 单窗口全自绘 —— 核心数据结构与控件声明
//
// 整个界面只有一个 HWND：所有控件都是"矩形 + 绘制函数 + 命中测试"，
// 没有任何子窗口。拖动/悬停只是修改数据并重绘脏区。
// 因此不存在鼠标捕获泄漏、拖动状态残留、z 序重排、控件隐藏/恢复等问题。
// ---------------------------------------------------------------------------

namespace U {
    const int SidebarW  = 218;
    const int Pad       = 14;
    const int RowGap    = 14;
    const int LabelH    = 16;
    const int InputH    = 30;
    const int BtnH      = 30;
    const int TitleH    = 34;
    const int Grip      = 14;
    const int MinPanelW = 250;
    const int MinPanelH = 110;
    const int LogRowH   = 19;
    const int Radius    = 12;
}

struct App;

// ---------------------------------------------------------------------------
// 控件基类
// ---------------------------------------------------------------------------
struct W {
    RECT rc{};
    bool visible = true;
    bool hover = false;
    bool focused = false;

    virtual ~W() = default;
    virtual void paint(HDC dc, App& app) = 0;

    virtual bool focusable() const { return false; }
    virtual void on_down(POINT) {}
    virtual void on_up(POINT) {}
    virtual void on_move(POINT) {}
    virtual bool on_char(wchar_t) { return false; }
    virtual bool on_key(int, bool) { return false; }
    virtual void on_wheel(int) {}
};

// 上下文（字体等共享资源由主窗口持有）
struct App {
    HFONT f_brand = nullptr;
    HFONT f_h2 = nullptr;
    HFONT f_body = nullptr;
    HFONT f_bold = nullptr;
    HFONT f_small = nullptr;
    HFONT f_log = nullptr;
    HINSTANCE inst = nullptr;
    bool bot_running = false;
    int  msg_count = 0;
    std::wstring model_name;
};

// ---------------------------------------------------------------------------
// 静态文字
// ---------------------------------------------------------------------------
struct Label : W {
    std::wstring text;
    COLORREF color = CLR_INVALID;
    HFONT font = nullptr;
    void paint(HDC dc, App& app) override;
};

// ---------------------------------------------------------------------------
// 按钮
// ---------------------------------------------------------------------------
struct Button : W {
    std::wstring text;
    bool primary = false;
    bool danger = false;
    std::function<void()> click;
    HFONT font = nullptr;
    void paint(HDC dc, App& app) override;
    void on_down(POINT) override;
    void on_up(POINT) override;
};

// ---------------------------------------------------------------------------
// 输入框（单行 / 多行，自绘光标与选区）
// ---------------------------------------------------------------------------
struct Field : W {
    std::wstring text;
    std::wstring placeholder;
    bool password = false;
    bool multiline = false;
    int  caret = 0;
    int  anchor = -1;
    int  scroll = 0;
    int  reserve_right = 0;   // 右侧为其它元素预留的宽度（如 Combo 的下拉箭头）

    bool focusable() const override { return true; }
    void paint(HDC dc, App& app) override;
    void on_down(POINT p) override;
    bool on_char(wchar_t c) override;
    bool on_key(int vk, bool ctrl) override;

    std::wstring shown() const;
    static int measure(HDC dc, const std::wstring& s);
    static std::vector<std::wstring> split_lines(const std::wstring& s);
    int px_of_index(HDC dc, const std::wstring& s, int idx) const;
    int index_of_px(HDC dc, const std::wstring& s, int px) const;
};

// ---------------------------------------------------------------------------
// 下拉选择（可编辑）
//
// 模型名迭代很快，如果只能从固定列表里选，出了新模型就得改代码重新发版。
// 所以 Combo 直接继承 Field：框体里可以像输入框一样打字输入任意模型名，
// 点右侧箭头才展开预设列表。既能快速选，也能填列表里没有的新模型。
//   text   = 当前模型名（可编辑）
//   index  = 命中的预设项下标，-1 表示自定义
// ---------------------------------------------------------------------------
struct Combo : Field {
    std::vector<std::wstring> items;
    int index = -1;
    bool open = false;

    void paint(HDC dc, App& app) override;
    void on_down(POINT p) override;

    void paint_popup(HDC dc, App& app);
    RECT popup_rect() const;
    bool in_arrow(POINT p) const;
    void choose(int i);
    void sync_index();          // text 变了以后刷新 index
};

// ---------------------------------------------------------------------------
// 复选框
// ---------------------------------------------------------------------------
struct Check : W {
    std::wstring text;
    bool checked = false;
    std::function<void()> on_change;
    void paint(HDC dc, App& app) override;
    void on_up(POINT p) override;
};

// ---------------------------------------------------------------------------
// 日志视图（虚拟滚动，只画可见行）
// ---------------------------------------------------------------------------
struct LogLine {
    std::wstring text;
    int level = 0;      // 0 信息 1 AI 2 成功 3 警告 4 错误
};

struct LogView : W {
    std::vector<LogLine> lines;
    int top = 0;
    bool follow = true;

    void paint(HDC dc, App& app) override;
    void on_wheel(int delta) override;

    int rows_per_page() const;
    void scroll_to_end();
    void clamp_top();
};

// ---------------------------------------------------------------------------
// 通用绘制助手
// ---------------------------------------------------------------------------
namespace draw {
    void text(HDC dc, HFONT f, COLORREF c, const RECT& rc, const std::wstring& s, UINT flags);
    void round_rect(HDC dc, const RECT& rc, int r, HBRUSH br, HPEN pen);
    void pill(HDC dc, const RECT& rc, HBRUSH br, COLORREF fg,
              const std::wstring& s, HFONT f);
    void card(HDC dc, const RECT& rc, int r);
}

// utf8 <-> wstring
std::wstring u2w(const std::string& s);
std::string  w2u(const std::wstring& w);

// 写文件日志：logs/app.log
// 服务器上跑的是 GUI 程序，没有控制台可看，出问题只能靠日志文件排查。
// 所有关键事件（连接、配置变更、错误）都往这里落一份。
void file_log(const std::string& msg);

// 日志级别判定
int log_level_of(const std::string& raw);
