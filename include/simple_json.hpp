#pragma once
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <initializer_list>


// ---------------------------------------------------------------------------
// JSON 字符串转义（带 UTF-8 合法性校验）
//
// 原实现只处理了 " \ \b \f \n \r \t，其余字节原样输出，存在两个漏洞：
//   1. 其它控制字符（0x01-0x07、0x0B、0x0E-0x1F）没转义 -> 请求体不是合法 JSON
//   2. 非法 UTF-8 字节没校验没替换 -> 服务端直接回
//      "invalid unicode code point"（400），而提示词本身完全看不出问题
// 这里做严格校验：合法序列原样输出，非法字节与孤立代理项替换成 U+FFFD。
// ---------------------------------------------------------------------------
inline void json_escape_string(const std::string& in, std::string& out) {
    static const char* hex = "0123456789abcdef";
    out.reserve(in.size() + 16);
    size_t i = 0, n = in.size();
    auto put_replacement = [&]() {
        out += "\xEF\xBF\xBD";   // U+FFFD
    };
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == '"')       { out += "\\\""; ++i; continue; }
        if (c == '\\')      { out += "\\\\"; ++i; continue; }
        if (c == '\b')      { out += "\\b";  ++i; continue; }
        if (c == '\f')      { out += "\\f";  ++i; continue; }
        if (c == '\n')      { out += "\\n";  ++i; continue; }
        if (c == '\r')      { out += "\\r";  ++i; continue; }
        if (c == '\t')      { out += "\\t";  ++i; continue; }
        if (c < 0x20) {
            // 其余控制字符必须转义，否则 JSON 非法
            out += "\\u00";
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
            ++i;
            continue;
        }
        if (c < 0x80) { out += static_cast<char>(c); ++i; continue; }

        // 多字节序列：校验完整性
        size_t len = 0;
        uint32_t cp = 0;
        if ((c & 0xE0) == 0xC0)      { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else { put_replacement(); ++i; continue; }   // 非法起始字节

        if (i + len > n) { put_replacement(); ++i; continue; }
        bool ok = true;
        for (size_t k = 1; k < len; ++k) {
            unsigned char cc = static_cast<unsigned char>(in[i + k]);
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { put_replacement(); ++i; continue; }

        bool bad = false;
        if (len == 2 && cp < 0x80) bad = true;
        if (len == 3 && cp < 0x800) bad = true;
        if (len == 4 && cp < 0x10000) bad = true;
        if (cp > 0x10FFFF) bad = true;
        if (cp >= 0xD800 && cp <= 0xDFFF) bad = true;   // 代理项
        if (cp == 0xFFFE || cp == 0xFFFF) bad = true;   // 非字符码点
        if (bad) { put_replacement(); i += len; continue; }

        out.append(in, i, len);
        i += len;
    }
}

namespace simplejson {
    enum Type { TYPE_NULL, TYPE_BOOL, TYPE_NUMBER, TYPE_STRING, TYPE_ARRAY, TYPE_OBJECT };

    struct Value {
        Type type = TYPE_NULL;
        bool bool_val = false;
        double num_val = 0.0;
        int64_t int_val = 0;
        std::string str_val;
        std::vector<Value> arr_val;
        std::map<std::string, Value> obj_val;

        Value() : type(TYPE_NULL) {}
        Value(bool b) : type(TYPE_BOOL), bool_val(b) {}
        Value(int n) : type(TYPE_NUMBER), num_val(n), int_val(n) {}
        Value(int64_t n) : type(TYPE_NUMBER), num_val(static_cast<double>(n)), int_val(n) {}
        Value(double n) : type(TYPE_NUMBER), num_val(n), int_val(static_cast<int64_t>(n)) {}
        Value(const char* s) : type(TYPE_STRING), str_val(s ? s : "") {}
        Value(const std::string& s) : type(TYPE_STRING), str_val(s) {}

        // 支持 std::initializer_list 构造与赋值：区分列表/对象
        Value(std::initializer_list<Value> list) {
            type = TYPE_ARRAY;
            arr_val.assign(list.begin(), list.end());
        }

        Value(std::initializer_list<std::pair<const std::string, Value>> list) {
            type = TYPE_OBJECT;
            for (const auto& kv : list) {
                obj_val[kv.first] = kv.second;
            }
        }

        Value& operator=(std::initializer_list<Value> list) {
            type = TYPE_ARRAY;
            arr_val.assign(list.begin(), list.end());
            obj_val.clear();
            return *this;
        }

        Value& operator=(std::initializer_list<std::pair<const std::string, Value>> list) {
            type = TYPE_OBJECT;
            arr_val.clear();
            obj_val.clear();
            for (const auto& kv : list) {
                obj_val[kv.first] = kv.second;
            }
            return *this;
        }

        bool is_null() const { return type == TYPE_NULL; }
        bool is_boolean() const { return type == TYPE_BOOL; }
        bool is_number() const { return type == TYPE_NUMBER; }
        bool is_string() const { return type == TYPE_STRING; }
        bool is_array() const { return type == TYPE_ARRAY; }
        bool is_object() const { return type == TYPE_OBJECT; }
        bool empty() const {
            if (is_array()) return arr_val.empty();
            if (is_object()) return obj_val.empty();
            if (is_string()) return str_val.empty();
            return type == TYPE_NULL;
        }
        size_t size() const {
            if (is_array()) return arr_val.size();
            if (is_object()) return obj_val.size();
            return 0;
        }

        template<typename T>
        T get() const;

        bool contains(const std::string& key) const {
            if (type != TYPE_OBJECT) return false;
            return obj_val.find(key) != obj_val.end();
        }

        Value& operator[](const std::string& key) {
            if (type != TYPE_OBJECT) {
                type = TYPE_OBJECT;
                obj_val.clear();
                arr_val.clear();
            }
            return obj_val[key];
        }

        const Value& operator[](const std::string& key) const {
            static const Value null_val;
            if (type != TYPE_OBJECT) return null_val;
            auto it = obj_val.find(key);
            return (it != obj_val.end()) ? it->second : null_val;
        }

        Value& operator[](size_t index) {
            if (type != TYPE_ARRAY) {
                type = TYPE_ARRAY;
                arr_val.clear();
                obj_val.clear();
            }
            if (index >= arr_val.size()) arr_val.resize(index + 1);
            return arr_val[index];
        }

        const Value& operator[](size_t index) const {
            static const Value null_val;
            if (type != TYPE_ARRAY || index >= arr_val.size()) return null_val;
            return arr_val[index];
        }

        void push_back(const Value& v) {
            if (type != TYPE_ARRAY) {
                type = TYPE_ARRAY;
                arr_val.clear();
                obj_val.clear();
            }
            arr_val.push_back(v);
        }

        std::string dump(int indent = -1, int current_indent = 0) const {
            std::ostringstream ss;
            switch (type) {
                case TYPE_NULL: return "null";
                case TYPE_BOOL: return bool_val ? "true" : "false";
                case TYPE_NUMBER: {
                    if (num_val == static_cast<double>(int_val)) return std::to_string(int_val);
                    return std::to_string(num_val);
                }
                case TYPE_STRING: {
                    std::string esc;
                    json_escape_string(str_val, esc);
                    ss << "\"" << esc << "\"";
                    return ss.str();
                }
                case TYPE_ARRAY: {
                    if (arr_val.empty()) return "[]";
                    ss << "[";
                    for (size_t i = 0; i < arr_val.size(); ++i) {
                        if (i > 0) ss << ",";
                        if (indent > 0) ss << "\n" << std::string(current_indent + indent, ' ');
                        ss << arr_val[i].dump(indent, current_indent + indent);
                    }
                    if (indent > 0) ss << "\n" << std::string(current_indent, ' ');
                    ss << "]";
                    return ss.str();
                }
                case TYPE_OBJECT: {
                    if (obj_val.empty()) return "{}";
                    ss << "{";
                    size_t count = 0;
                    for (auto const& kv : obj_val) {
                        if (count++ > 0) ss << ",";
                        if (indent > 0) ss << "\n" << std::string(current_indent + indent, ' ');
                        ss << "\"" << kv.first << "\":" << (indent > 0 ? " " : "") << kv.second.dump(indent, current_indent + indent);
                    }
                    if (indent > 0) ss << "\n" << std::string(current_indent, ' ');
                    ss << "}";
                    return ss.str();
                }
            }
            return "null";
        }

        static Value parse(const std::string& str);
        static Value array() { Value v; v.type = TYPE_ARRAY; return v; }
        static Value object() { Value v; v.type = TYPE_OBJECT; return v; }

        auto begin() { return arr_val.begin(); }
        auto end() { return arr_val.end(); }
        auto begin() const { return arr_val.begin(); }
        auto end() const { return arr_val.end(); }
    };

    template<> inline std::string Value::get<std::string>() const { return is_string() ? str_val : ""; }
    template<> inline bool Value::get<bool>() const { return is_boolean() ? bool_val : false; }
    template<> inline int Value::get<int>() const { return is_number() ? static_cast<int>(int_val) : 0; }
    template<> inline int64_t Value::get<int64_t>() const { return is_number() ? int_val : 0; }
    template<> inline float Value::get<float>() const { return is_number() ? static_cast<float>(num_val) : 0.0f; }
    template<> inline double Value::get<double>() const { return is_number() ? num_val : 0.0; }
    template<> inline std::vector<std::string> Value::get<std::vector<std::string>>() const {
        std::vector<std::string> res;
        if (is_array()) {
            for (const auto& item : arr_val) {
                if (item.is_string()) res.push_back(item.str_val);
            }
        }
        return res;
    }

    class Parser {
        const std::string& src_;
        size_t idx_ = 0;

        void skip_ws() {
            while (idx_ < src_.size() && (src_[idx_] == ' ' || src_[idx_] == '\t' || src_[idx_] == '\r' || src_[idx_] == '\n')) {
                idx_++;
            }
        }

    public:
        // start：跳过前导字节（例如 UTF-8 BOM）而不必构造临时字符串。
        // 注意 src_ 是引用，绝不能把 substr 的临时量传进来。
        Parser(const std::string& s, size_t start = 0) : src_(s), idx_(start) {}

        Value parse() {
            skip_ws();
            if (idx_ >= src_.size()) return Value();
            char c = src_[idx_];
            if (c == '{') return parse_object();
            if (c == '[') return parse_array();
            if (c == '"') return parse_string();
            if (c == 't' || c == 'f') return parse_bool();
            if (c == 'n') return parse_null();
            if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
            return Value();
        }

        Value parse_null() {
            if (src_.substr(idx_, 4) == "null") { idx_ += 4; return Value(); }
            return Value();
        }

        Value parse_bool() {
            if (src_.substr(idx_, 4) == "true") { idx_ += 4; return Value(true); }
            if (src_.substr(idx_, 5) == "false") { idx_ += 5; return Value(false); }
            return Value();
        }

        Value parse_string() {
            idx_++; // skip leading "
            std::string res;
            while (idx_ < src_.size()) {
                char c = src_[idx_++];
                if (c == '"') return Value(res);
                if (c == '\\' && idx_ < src_.size()) {
                    char esc = src_[idx_++];
                    if (esc == '"') res += '"';
                    else if (esc == '\\') res += '\\';
                    else if (esc == '/') res += '/';
                    else if (esc == 'b') res += '\b';
                    else if (esc == 'f') res += '\f';
                    else if (esc == 'n') res += '\n';
                    else if (esc == 'r') res += '\r';
                    else if (esc == 't') res += '\t';
                    else if (esc == 'u' && idx_ + 4 <= src_.size()) {
                        std::string hex_str = src_.substr(idx_, 4);
                        idx_ += 4;
                        try {
                            unsigned int codepoint = std::stoul(hex_str, nullptr, 16);
                            if (codepoint < 0x80) res += static_cast<char>(codepoint);
                            else if (codepoint < 0x800) {
                                res += static_cast<char>(0xC0 | (codepoint >> 6));
                                res += static_cast<char>(0x80 | (codepoint & 0x3F));
                            } else {
                                res += static_cast<char>(0xE0 | (codepoint >> 12));
                                res += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                                res += static_cast<char>(0x80 | (codepoint & 0x3F));
                            }
                        } catch (...) {}
                    } else res += esc;
                } else {
                    res += c;
                }
            }
            return Value(res);
        }

        Value parse_number() {
            size_t start = idx_;
            if (src_[idx_] == '-') idx_++;
            bool is_float = false;
            while (idx_ < src_.size() && ((src_[idx_] >= '0' && src_[idx_] <= '9') || src_[idx_] == '.' || src_[idx_] == 'e' || src_[idx_] == 'E' || src_[idx_] == '+' || src_[idx_] == '-')) {
                if (src_[idx_] == '.' || src_[idx_] == 'e' || src_[idx_] == 'E') is_float = true;
                idx_++;
            }
            std::string num_str = src_.substr(start, idx_ - start);
            try {
                if (is_float) return Value(std::stod(num_str));
                return Value(static_cast<int64_t>(std::stoll(num_str)));
            } catch (...) {
                return Value(0);
            }
        }

        Value parse_array() {
            idx_++; // skip [
            Value res = Value::array();
            skip_ws();
            if (idx_ < src_.size() && src_[idx_] == ']') { idx_++; return res; }
            while (idx_ < src_.size()) {
                res.push_back(parse());
                skip_ws();
                if (idx_ >= src_.size()) break;
                if (src_[idx_] == ']') { idx_++; break; }
                if (src_[idx_] == ',') { idx_++; skip_ws(); }
            }
            return res;
        }

        Value parse_object() {
            idx_++; // skip {
            Value res = Value::object();
            skip_ws();
            if (idx_ < src_.size() && src_[idx_] == '}') { idx_++; return res; }
            while (idx_ < src_.size()) {
                skip_ws();
                if (idx_ >= src_.size() || src_[idx_] != '"') break;
                std::string key = parse_string().str_val;
                skip_ws();
                if (idx_ < src_.size() && src_[idx_] == ':') { idx_++; skip_ws(); }
                res[key] = parse();
                skip_ws();
                if (idx_ >= src_.size()) break;
                if (src_[idx_] == '}') { idx_++; break; }
                if (src_[idx_] == ',') { idx_++; skip_ws(); }
            }
            return res;
        }
    };

    inline Value Value::parse(const std::string& str) {
        // 跳过 UTF-8 BOM：Windows 上不少编辑器/脚本（PowerShell 的
        // Set-Content -Encoding UTF8 就是）写文件会带 BOM，
        // 之前会让解析静默返回非对象，非常难查。
        // 偏移交给 Parser，绝不构造临时子串：src_ 是引用，临时量会悬空。
        size_t start = 0;
        if (str.size() >= 3 &&
            static_cast<unsigned char>(str[0]) == 0xEF &&
            static_cast<unsigned char>(str[1]) == 0xBB &&
            static_cast<unsigned char>(str[2]) == 0xBF) {
            start = 3;
        }
        Parser p(str, start);
        return p.parse();
    }
}

// 别名兼容
namespace nlohmann {
    using json = simplejson::Value;
}

inline std::istream& operator>>(std::istream& is, simplejson::Value& v) {
    std::string str((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    v = simplejson::Value::parse(str);
    return is;
}

inline std::ostream& operator<<(std::ostream& os, const simplejson::Value& v) {
    os << v.dump(4);
    return os;
}
