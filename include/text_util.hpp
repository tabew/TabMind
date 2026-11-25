#pragma once
#include <string>

// ---------------------------------------------------------------------------
// 文本处理小工具
// ---------------------------------------------------------------------------

namespace textutil {

inline bool starts_with(const std::string& s, const char* p, size_t n) {
    return s.size() >= n && s.compare(0, n, p, n) == 0;
}

inline bool ends_with(const std::string& s, const char* p, size_t n) {
    return s.size() >= n && s.compare(s.size() - n, n, p, n) == 0;
}

// 去掉模型输出首尾的空白与包裹引号。
//
// 注意：不能写 `c == '「'` —— 一个 char 只装得下一个字节，
// 「 在 UTF-8 里是 E3 80 8C 三个字节，比较结果永远为假，
// MSVC 只给个多字符常量警告，GCC 直接当实现定义的 int 处理。
// 所以这里按字节序列匹配。
inline void strip_wrapping_quotes(std::string& s) {
    auto is_space_or_ascii_quote = [](char c) {
        return c == ' ' || c == '\n' || c == '\r' || c == '\t' ||
               c == '"' || c == '\'';
    };

    while (!s.empty()) {
        if (is_space_or_ascii_quote(s.front())) {
            s.erase(s.begin());
            continue;
        }
        if (starts_with(s, "\xE3\x80\x8C", 3) ||   // 「
            starts_with(s, "\xE2\x80\x9C", 3) ||   // “
            starts_with(s, "\xEF\xBB\xBF", 3)) {   // UTF-8 BOM
            s.erase(0, 3);
            continue;
        }
        break;
    }

    while (!s.empty()) {
        if (is_space_or_ascii_quote(s.back())) {
            s.pop_back();
            continue;
        }
        if (ends_with(s, "\xE3\x80\x8D", 3) ||     // 」
            ends_with(s, "\xE2\x80\x9D", 3)) {     // ”
            s.erase(s.size() - 3);
            continue;
        }
        break;
    }
}

}  // namespace textutil
