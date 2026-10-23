#include "ai_client.hpp"
#include "simple_http.hpp"
#include "simple_json.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

// ---------- UTF-8 字数统计 / 截断（“不超过 1000 字”按字符计） ----------
static size_t utf8_length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

static std::string utf8_truncate(const std::string& s, size_t max_chars) {
    if (utf8_length(s) <= max_chars) return s;
    size_t n = 0, i = 0;
    while (i < s.size() && n < max_chars) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t adv = 1;
        if ((c & 0x80) == 0) adv = 1;
        else if ((c & 0xE0) == 0xC0) adv = 2;
        else if ((c & 0xF0) == 0xE0) adv = 3;
        else if ((c & 0xF8) == 0xF0) adv = 4;
        if (i + adv > s.size()) break;
        i += adv;
        ++n;
    }
    return s.substr(0, i);
}

// ---------- Base64 ----------
static const char* B64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const std::string& in) {
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < in.size()) {
        unsigned int n = (static_cast<unsigned char>(in[i]) << 16)
                       | (static_cast<unsigned char>(in[i + 1]) << 8)
                       | static_cast<unsigned char>(in[i + 2]);
        out.push_back(B64_CHARS[(n >> 18) & 63]);
        out.push_back(B64_CHARS[(n >> 12) & 63]);
        out.push_back(B64_CHARS[(n >> 6) & 63]);
        out.push_back(B64_CHARS[n & 63]);
        i += 3;
    }
    size_t rem = in.size() - i;
    if (rem == 1) {
        unsigned int n = static_cast<unsigned char>(in[i]) << 16;
        out.push_back(B64_CHARS[(n >> 18) & 63]);
        out.push_back(B64_CHARS[(n >> 12) & 63]);
        out += "==";
    } else if (rem == 2) {
        unsigned int n = (static_cast<unsigned char>(in[i]) << 16)
                       | (static_cast<unsigned char>(in[i + 1]) << 8);
        out.push_back(B64_CHARS[(n >> 18) & 63]);
        out.push_back(B64_CHARS[(n >> 12) & 63]);
        out.push_back(B64_CHARS[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

static std::string guess_mime(const std::string& path) {
    std::string p = path;
    std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (p.find(".png") != std::string::npos) return "image/png";
    if (p.find(".gif") != std::string::npos) return "image/gif";
    if (p.find(".webp") != std::string::npos) return "image/webp";
    if (p.find(".bmp") != std::string::npos) return "image/bmp";
    return "image/jpeg";
}

// 删除本地图片（看完即删，省磁盘）
static void remove_local_image(const std::string& path_or_url) {
    if (path_or_url.empty()) return;
    if (path_or_url.find("http://") == 0 || path_or_url.find("https://") == 0 ||
        path_or_url.find("data:") == 0) {
        return; // 远程/data URL 不动
    }

    std::string path = path_or_url;
    const std::string file_proto = "file:///";
    if (path.find(file_proto) == 0) {
        path = path.substr(file_proto.size());
    } else if (path.find("file://") == 0) {
        path = path.substr(7);
    }

    std::error_code ec;
    if (fs::exists(path, ec)) {
        fs::remove(path, ec);
    }
    // 相对 data/images 兜底
    std::string base = path;
    size_t pos = base.find_last_of("/\\");
    if (pos != std::string::npos) base = base.substr(pos + 1);
    if (!base.empty()) {
        fs::remove("data/images/" + base, ec);
    }
}

// 将本地图片转成 data URL；过大或读失败返回空
// delete_after=true 时本地图片读入内存后立即删除（看完即删）
static std::string image_to_data_url(const std::string& path_or_url, bool delete_after = true) {
    if (path_or_url.empty()) return "";

    // 已是远程 URL / data URL，直接透传
    if (path_or_url.find("http://") == 0 || path_or_url.find("https://") == 0 ||
        path_or_url.find("data:") == 0) {
        return path_or_url;
    }

    std::string path = path_or_url;
    // 去掉 file:/// 前缀
    const std::string file_proto = "file:///";
    if (path.find(file_proto) == 0) {
        path = path.substr(file_proto.size());
    } else if (path.find("file://") == 0) {
        path = path.substr(7);
    }

    // 相对路径补全
    std::string candidates[] = {
        path,
        "data/images/" + path,
        path
    };

    std::string resolved;
    for (const auto& c : candidates) {
        if (fs::exists(c)) {
            resolved = c;
            break;
        }
    }
    if (resolved.empty()) return "";

    std::string result;
    try {
        auto sz = fs::file_size(resolved);
        // 超过 1.2MB 不送视觉（base64 后更大，容易把请求打挂）
        if (sz > 0 && sz <= 1200 * 1024) {
            std::ifstream ifs(resolved, std::ios::binary);
            if (ifs.is_open()) {
                std::string bytes((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                if (!bytes.empty()) {
                    result = "data:" + guess_mime(resolved) + ";base64," + base64_encode(bytes);
                }
            }
        }
    } catch (...) {}

    // 读完（或放弃超大图）后立刻删除本地文件，省存储
    if (delete_after) {
        remove_local_image(resolved);
        remove_local_image(path_or_url);
    }
    return result;
}

// 从 CQ 码文本中提取本地图片路径
static std::vector<std::string> extract_image_refs(const std::string& text) {
    std::vector<std::string> paths;
    size_t pos = 0;
    const std::string key = "[CQ:image,file=";
    while ((pos = text.find(key, pos)) != std::string::npos) {
        size_t start = pos + key.size();
        size_t end = text.find(']', start);
        if (end == std::string::npos) break;
        std::string val = text.substr(start, end - start);
        // 去掉可能的 url= 等附加
        size_t comma = val.find(',');
        if (comma != std::string::npos) val = val.substr(0, comma);
        if (!val.empty()) paths.push_back(val);
        pos = end + 1;
    }
    return paths;
}

AIClient::AIClient(const AIConfig& config)
    : cfg_(std::make_shared<AIConfig>(config)) {
    parse_base_url(cfg_get()->base_url);
    load_summaries_from_disk();
    mem_thread_ = std::thread(&AIClient::mem_worker_loop, this);

    // 启动时检查 API Key，避免上线后才发现 401
    if (cfg_get()->api_key.empty()
        || cfg_get()->api_key.find("在这里填入") != std::string::npos
        || cfg_get()->api_key.find("your-") != std::string::npos
        || cfg_get()->api_key.find("sk-your") != std::string::npos) {
        std::cerr << "[AIClient] 警告: config.json 里的 ai.api_key 还是占位符/空的，调用小米 MiMo 会 401 Invalid API Key。\n"
                  << "          请到 https://platform.xiaomimimo.com/ 创建 API Key，并写入 config.json 的 ai.api_key。\n";
    }
}

AIClient::~AIClient() {
    mem_running_ = false;
    mem_cv_.notify_all();
    if (mem_thread_.joinable()) {
        mem_thread_.join();
    }
    // 退出前尽量把缓冲简写掉（同步、仅一次）
    try {
        std::vector<std::string> ids;
        {
            std::lock_guard<std::mutex> lock(memory_mutex_);
            for (const auto& kv : memory_buffer_) {
                if (!kv.second.empty()) ids.push_back(kv.first);
            }
        }
        for (const auto& id : ids) {
            do_summarize(id);
        }
        save_summaries_to_disk();
    } catch (...) {}
}

void AIClient::parse_base_url(const std::string& url) {
    host_ = url;
}

// ---------- 每群/每人独立记忆，写成可读 txt ----------
// 目录: data/memories/group_123.txt / data/memories/private_456.txt

static std::string memory_dir() { return "data/memories"; }

static std::string memory_txt_path(const std::string& context_id) {
    // context_id 形如 group_123 / private_456，已可直接当文件名
    std::string safe;
    for (char c : context_id) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            safe.push_back(c);
        } else {
            safe.push_back('_');
        }
    }
    if (safe.empty()) safe = "unknown";
    return memory_dir() + "/" + safe + ".txt";
}

static std::string now_time_str() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

// 把某条会话记忆写成人类可读 txt（可直接打开看）
static void write_memory_txt(const std::string& context_id, const std::string& summary, int max_chars) {
    try {
        fs::create_directories(memory_dir());
        std::string path = memory_txt_path(context_id);
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open()) return;

        std::string kind = "未知会话";
        std::string id_part = context_id;
        if (context_id.rfind("group_", 0) == 0) {
            kind = "群聊";
            id_part = context_id.substr(6);
        } else if (context_id.rfind("private_", 0) == 0) {
            kind = "私聊";
            id_part = context_id.substr(8);
        }

        size_t chars = utf8_length(summary);
        ofs << "========================================\n"
            << "塔菲长期记忆（可直接编辑本文件，重启后生效）\n"
            << "========================================\n"
            << "会话ID: " << context_id << "\n"
            << "类型: " << kind << "\n"
            << "对象: " << id_part << "\n"
            << "更新时间: " << now_time_str() << "\n"
            << "字数: " << chars << " / " << max_chars << "\n"
            << "----------------------------------------\n"
            << summary << "\n"
            << "----------------------------------------\n"
            << "（以上为自动简写记忆；手动改完保存即可，不超过 1000 字）\n";
    } catch (...) {}
}

void AIClient::load_summaries_from_disk() {
    std::lock_guard<std::mutex> lock(memory_mutex_);
    try {
        fs::create_directories(memory_dir());

        // 1) 优先读 data/memories/*.txt（每个群/私聊一份）
        if (fs::exists(memory_dir()) && fs::is_directory(memory_dir())) {
            for (const auto& entry : fs::directory_iterator(memory_dir())) {
                if (!entry.is_regular_file()) continue;
                auto p = entry.path();
                if (p.extension() != ".txt") continue;
                std::string ctx_id = p.stem().string();
                std::ifstream ifs(p, std::ios::binary);
                if (!ifs.is_open()) continue;
                std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

                // 去掉文件头，只留摘要正文
                const std::string sep = "----------------------------------------\n";
                std::string body = content;
                size_t first = content.find(sep);
                if (first != std::string::npos) {
                    size_t body_start = first + sep.size();
                    size_t second = content.find(sep, body_start);
                    if (second != std::string::npos) {
                        body = content.substr(body_start, second - body_start);
                    } else {
                        body = content.substr(body_start);
                    }
                }
                // 去首尾空白
                auto s = body.find_first_not_of(" \t\r\n");
                auto e = body.find_last_not_of(" \t\r\n");
                if (s == std::string::npos) continue;
                body = body.substr(s, e - s + 1);

                int maxc = cfg_get()->memory_max_chars > 0 ? cfg_get()->memory_max_chars : 1000;
                memory_summaries_[ctx_id] = utf8_truncate(body, static_cast<size_t>(maxc));
            }
        }

        // 2) 兼容迁移老的 chat_history.json / memory_summary.json
        auto migrate_json = [&](const std::string& path) {
            if (!fs::exists(path)) return;
            std::ifstream ifs(path);
            if (!ifs.is_open()) return;
            std::string str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            nlohmann::json j = nlohmann::json::parse(str);
            if (!j.is_object()) return;
            for (const auto& kv : j.obj_val) {
                if (!kv.second.is_string()) continue;
                if (memory_summaries_.count(kv.first)) continue; // txt 优先
                std::string s = kv.second.get<std::string>();
                int maxc = cfg_get()->memory_max_chars > 0 ? cfg_get()->memory_max_chars : 1000;
                s = utf8_truncate(s, static_cast<size_t>(maxc));
                memory_summaries_[kv.first] = s;
                write_memory_txt(kv.first, s, maxc);
            }
        };
        migrate_json("data/chat_history.json");
        migrate_json("data/memory_summary.json");
    } catch (...) {}
}

void AIClient::save_summaries_to_disk() {
    std::lock_guard<std::mutex> lock(memory_mutex_);
    try {
        int maxc = cfg_get()->memory_max_chars > 0 ? cfg_get()->memory_max_chars : 1000;
        fs::create_directories(memory_dir());
        for (const auto& [ctx_id, sum_text] : memory_summaries_) {
            if (!sum_text.empty()) {
                write_memory_txt(ctx_id, sum_text, maxc);
            }
        }
    } catch (...) {}
}

void AIClient::clear_history(const std::string& context_id) {
    {
        std::lock_guard<std::mutex> lock(memory_mutex_);
        memory_summaries_.erase(context_id);
        recent_sessions_.erase(context_id);
        memory_buffer_.erase(context_id);
    }
    // 删掉对应的 txt
    try {
        std::error_code ec;
        fs::remove(memory_txt_path(context_id), ec);
    } catch (...) {}
    save_summaries_to_disk();
}

// 按 base_url 推断该用哪种鉴权头。
// DeepSeek 官方 = Authorization: Bearer；小米 MiMo = api-key:；
// 其它中转网关两种都收，用 both 最保险。
static std::string infer_auth_style(const std::string& base_url, const std::string& configured) {
    if (configured == "bearer" || configured == "api-key" || configured == "both") {
        return configured;
    }
    // configured 为空或 "auto"：按 host 判断
    if (base_url.find("xiaomimimo") != std::string::npos) return "api-key";
    if (base_url.find("deepseek") != std::string::npos) return "bearer";
    return "both";
}

std::string AIClient::send_http_request(const std::string& req_body) {
    // 小米 MiMo 官方只认 api-key 头；若同时塞 Authorization: Bearer 可能被网关判为无效密钥
    std::string auth = infer_auth_style(cfg_get()->base_url, cfg_get()->auth_style);
    auto resp = simplehttp::post_json(cfg_get()->base_url, cfg_get()->api_key, req_body, auth);
    if (resp.status_code == 200) {
        return resp.body;
    }
    std::string err;
    if (resp.status_code == 0) {
        err = "HTTP 发送失败(0) 请求体大小=" + std::to_string(req_body.size())
            + " 请检查网络/超时/请求是否过大";
    } else {
        err = "HTTP " + std::to_string(resp.status_code) + " " + resp.body.substr(0, std::min<size_t>(resp.body.size(), 240));
    }
    std::cerr << "[AIClient] 请求失败: " << err << std::endl;
    last_error_ = err;
    return std::string("__HTTP_ERROR__") + std::to_string(resp.status_code) + "__" + resp.body;
}

// 热更新配置：原子替换整个配置对象。
// 之后所有请求都用新配置，不需要重启 bot、不需要断开重连。
// 取加锁的配置快照。所有对配置的读取都必须经过这里。
std::shared_ptr<const AIConfig> AIClient::cfg_get() const {
    std::lock_guard<std::mutex> lk(cfg_mtx_);
    return cfg_;
}

void AIClient::update_config(const AIConfig& cfg) {
    auto fresh = std::make_shared<AIConfig>(cfg);
    {
        std::lock_guard<std::mutex> lk(cfg_mtx_);
        cfg_ = fresh;
    }
    parse_base_url(fresh->base_url);
}

AIConfig AIClient::snapshot_config() const {
    std::lock_guard<std::mutex> lk(cfg_mtx_);
    return *cfg_;
}

// 拉取服务端可用模型列表：GET {base}/models
std::vector<std::string> AIClient::fetch_models(std::string& err) {
    std::vector<std::string> out;
    err.clear();

    std::string base = cfg_get()->base_url;
    while (!base.empty() && base.back() == '/') base.pop_back();
    size_t cut = base.find("/chat/completions");
    if (cut != std::string::npos) base = base.substr(0, cut);
    if (base.empty()) { err = "base_url 为空"; return out; }

    std::string auth = infer_auth_style(base, cfg_get()->auth_style);

    auto resp = simplehttp::get_auth(base + "/models", cfg_get()->api_key, auth);
    // 鉴权头被配错时（该用 Bearer 却发了 api-key，反之亦然）网关一律回 401，
    // 换个头再试一次，省得用户去猜到底该填哪种
    if (resp.status_code == 401 || resp.status_code == 403) {
        std::string alt = (auth == "api-key") ? "bearer" : "api-key";
        auto resp2 = simplehttp::get_auth(base + "/models", cfg_get()->api_key, alt);
        if (resp2.status_code == 200) { resp = resp2; auth = alt; }
    }
    if (resp.status_code != 200) {
        err = "HTTP " + std::to_string(resp.status_code) + " " +
              resp.body.substr(0, std::min<size_t>(resp.body.size(), 300));
        return out;
    }
    try {
        auto j = nlohmann::json::parse(resp.body);
        if (!j.contains("data") || !j["data"].is_array()) {
            err = "返回格式里没有 data 数组（可能不是 OpenAI 兼容接口）";
            return out;
        }
        for (auto& it : j["data"]) {
            if (it.contains("id") && it["id"].is_string()) {
                out.push_back(it["id"].get<std::string>());
            }
        }
    } catch (const std::exception& e) {
        err = std::string("返回内容不是合法 JSON: ") + e.what();
    }
    return out;
}

// 从错误响应里提取可读信息
static std::string extract_error_message(const std::string& raw) {
    if (raw.rfind("__HTTP_ERROR__", 0) == 0) {
        std::string rest = raw.substr(14);
        size_t sep = rest.find("__");
        std::string code = (sep == std::string::npos) ? rest : rest.substr(0, sep);
        std::string body = (sep == std::string::npos) ? "" : rest.substr(sep + 2);
        std::string detail;
        try {
            nlohmann::json j = nlohmann::json::parse(body);
            if (j.contains("message") && j["message"].is_string()) detail = j["message"].get<std::string>();
            else if (j.contains("error")) {
                if (j["error"].is_string()) detail = j["error"].get<std::string>();
                else if (j["error"].contains("message") && j["error"]["message"].is_string())
                    detail = j["error"]["message"].get<std::string>();
            }
        } catch (...) {}
        if (detail.empty()) detail = body.substr(0, std::min<size_t>(body.size(), 180));
        // 401 在这类网关上最常见的其实是"key 没有该模型的访问权限"，
        // 报错文本却只说 authentication fails，很容易被误判成 key 写错。
        if (code == "401") {
            detail += " | 401 常见原因：①Key 无效/过期/额度耗尽；"
                       "②该 Key 的分组没有当前模型的访问权限（网关权限层拦截，"
                       "常见报错含 governor）；③base_url 与 Key 不匹配"
                       "（厂商官方 Key 只能配该厂商的 base_url）"
                       "。建议点「拉取模型列表」看看服务端到底开放了哪些模型";
        }
        return "HTTP " + code + " " + detail;
    }
    if (raw.size() > 200) return raw.substr(0, 200);
    return raw;
}

std::string AIClient::call_model(const std::string& model, const nlohmann::json& messages,
                                 int max_tokens, float temperature) {
    nlohmann::json req_json = nlohmann::json::object();
    req_json["model"] = model.empty() ? std::string(cfg_get()->model) : model;
    req_json["messages"] = messages;
    // 显式 double，避免 float 隐式转成 bool
    req_json["temperature"] = static_cast<double>(temperature);
    req_json["max_tokens"] = max_tokens;
    // 仅在需要时发送 enable_thinking，避免部分网关拒识未知/false 字段
    if (cfg_get()->enable_thinking) {
        req_json["enable_thinking"] = true;
    }

    std::string body = req_json.dump();
    std::string resp_str = send_http_request(body);
    if (resp_str.empty()) {
        if (last_error_.empty()) last_error_ = "空响应";
        return "";
    }
    if (resp_str.rfind("__HTTP_ERROR__", 0) == 0) {
        std::string em = extract_error_message(resp_str);
        std::cerr << "[AIClient] " << em << std::endl;
        last_error_ = em;
        return "";
    }

    try {
        nlohmann::json resp_json = nlohmann::json::parse(resp_str);
        if (resp_json.contains("choices") && !resp_json["choices"].empty()) {
            const auto& msg = resp_json["choices"][0]["message"];
            std::string text;
            if (msg.contains("content") && msg["content"].is_string()) {
                text = msg["content"].get<std::string>();
            } else if (msg.contains("content") && msg["content"].is_array()) {
                for (const auto& part : msg["content"]) {
                    if (part.contains("text") && part["text"].is_string()) {
                        text += part["text"].get<std::string>();
                    }
                }
            }
            // content 为空时退回 reasoning_content（部分推理模型会这样）
            if (text.empty() && msg.contains("reasoning_content") && msg["reasoning_content"].is_string()) {
                text = msg["reasoning_content"].get<std::string>();
            }
            if (text.empty()) {
                last_error_ = "接口返回 content 为空: " + resp_str.substr(0, std::min<size_t>(resp_str.size(), 200));
            } else {
                last_error_.clear();
            }
            return text;
        }
        // 有些错误包也走 200
        last_error_ = "响应缺少 choices: " + resp_str.substr(0, std::min<size_t>(resp_str.size(), 200));
        std::cerr << "[AIClient] " << last_error_ << std::endl;
    } catch (const std::exception& e) {
        last_error_ = std::string("JSON 解析异常: ") + e.what();
        std::cerr << "[AIClient] " << last_error_ << std::endl;
    } catch (...) {
        last_error_ = "JSON 解析未知异常";
    }
    return "";
}

void AIClient::observe(const std::string& context_id, const std::string& line) {
    if (line.empty()) return;
    bool should_queue = false;
    {
        std::lock_guard<std::mutex> lock(memory_mutex_);
        auto& buf = memory_buffer_[context_id];
        buf.push_back(line);
        // 缓冲本身也限长，防止简写前膨胀
        while (buf.size() > 80) buf.pop_front();
        int need = cfg_get()->memory_batch_size > 0 ? cfg_get()->memory_batch_size : 8;
        should_queue = static_cast<int>(buf.size()) >= need;
    }
    // 攒够 → 丢给后台线程简写，绝不阻塞当前消息/回复
    if (should_queue) {
        enqueue_summarize(context_id);
    }
}

void AIClient::enqueue_summarize(const std::string& context_id) {
    {
        std::lock_guard<std::mutex> lock(mem_task_mutex_);
        if (mem_queued_.count(context_id)) return;
        mem_queued_.insert(context_id);
        mem_pending_.push_back(context_id);
    }
    mem_cv_.notify_one();
}

void AIClient::mem_worker_loop() {
    while (mem_running_) {
        std::string ctx;
        {
            std::unique_lock<std::mutex> lock(mem_task_mutex_);
            mem_cv_.wait(lock, [this]() {
                return !mem_running_ || !mem_pending_.empty();
            });
            if (!mem_running_ && mem_pending_.empty()) break;
            if (mem_pending_.empty()) continue;
            ctx = mem_pending_.front();
            mem_pending_.pop_front();
            mem_queued_.erase(ctx);
        }
        try {
            do_summarize(ctx);
        } catch (...) {}
    }
}

void AIClient::do_summarize(const std::string& context_id) {
    std::string old_summary;
    std::vector<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(memory_mutex_);
        auto it = memory_buffer_.find(context_id);
        if (it == memory_buffer_.end() || it->second.empty()) return;
        batch.assign(it->second.begin(), it->second.end());
        old_summary = memory_summaries_[context_id];
    }

    std::string new_summary = summarize_context(context_id, old_summary, batch);
    if (new_summary.empty()) {
        // 简写失败：本地硬截断兜底，保证记忆不无限涨
        std::lock_guard<std::mutex> lock(memory_mutex_);
        auto& buf = memory_buffer_[context_id];
        std::string joined = old_summary;
        for (const auto& ln : batch) {
            joined += " ";
            joined += ln;
        }
        int maxc = cfg_get()->memory_max_chars > 0 ? cfg_get()->memory_max_chars : 1000;
        memory_summaries_[context_id] = utf8_truncate(joined, static_cast<size_t>(maxc));
        buf.clear();
        write_memory_txt(context_id, memory_summaries_[context_id], maxc);
        return;
    }

    int maxc = cfg_get()->memory_max_chars > 0 ? cfg_get()->memory_max_chars : 1000;
    new_summary = utf8_truncate(new_summary, static_cast<size_t>(maxc));

    {
        std::lock_guard<std::mutex> lock(memory_mutex_);
        memory_summaries_[context_id] = new_summary;
        memory_buffer_[context_id].clear();
        int maxc = cfg_get()->memory_max_chars > 0 ? cfg_get()->memory_max_chars : 1000;
        write_memory_txt(context_id, new_summary, maxc);
    }
}

std::string AIClient::summarize_context(const std::string& context_id,
                                        const std::string& old_summary,
                                        const std::vector<std::string>& new_lines) {
    int maxc = cfg_get()->memory_max_chars > 0 ? cfg_get()->memory_max_chars : 1000;

    std::string joined;
    for (const auto& ln : new_lines) {
        joined += ln;
        joined += "\n";
    }
    // 输入也做硬截断，避免请求过大
    joined = utf8_truncate(joined, 3500);

    std::string sys =
        "你是群聊长期记忆压缩器。把已有记忆和新聊天记录合并成一份简洁摘要，写入记忆库。\n"
        "硬性要求：\n"
        "1. 摘要正文不超过 " + std::to_string(maxc) + " 个字符（汉字/字母/标点各算 1 字）；\n"
        "2. 只保留：关键人物（昵称/QQ）、关系、重要事件、偏好、在聊话题、群内梗；\n"
        "3. 用紧凑中文短句，不要客套，不要标题，不要解释；\n"
        "4. 图片消息用极短描述即可；\n"
        "5. 直接输出摘要正文，不要任何前缀。";

    nlohmann::json messages = nlohmann::json::array();
    nlohmann::json sys_msg = nlohmann::json::object();
    sys_msg["role"] = "system";
    sys_msg["content"] = sys;
    messages.push_back(sys_msg);

    std::string user_body = "【已有记忆】\n" + (old_summary.empty() ? "（无）" : old_summary)
        + "\n\n【新聊天记录】\n" + joined
        + "\n\n请输出合并后的记忆摘要（不超过 " + std::to_string(maxc) + " 字）。";

    nlohmann::json user_msg = nlohmann::json::object();
    user_msg["role"] = "user";
    user_msg["content"] = user_body;
    messages.push_back(user_msg);

    // 简写用更保守的采样
    std::string result = call_model(cfg_get()->model, messages, 800, 0.3f);
    // 兜底：去掉可能的引号/前缀
    while (!result.empty() && (result.front() == ' ' || result.front() == '\n' || result.front() == '"' || result.front() == '「')) {
        result.erase(result.begin());
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '\n' || result.back() == '"' || result.back() == '」')) {
        result.pop_back();
    }
    return result;
}

std::string AIClient::chat(const std::string& context_id, const std::string& user_prompt,
                           const std::vector<std::string>& image_paths,
                           bool allow_silent) {
    std::lock_guard<std::mutex> chat_lock(chat_mutex_);

    std::string current_summary;
    std::deque<ChatMessage> recent_msgs;

    {
        std::lock_guard<std::mutex> lock(memory_mutex_);
        current_summary = memory_summaries_[context_id];
        recent_msgs = recent_sessions_[context_id];
    }

    nlohmann::json messages = nlohmann::json::array();

    // 1. 系统主提示词 + 动态注入长期记忆摘要
    std::string full_sys_prompt = cfg_get()->system_prompt;
    if (!current_summary.empty()) {
        full_sys_prompt += "\n\n【关于本会话的已知长期记忆摘要】:\n" + current_summary;
    }
    if (allow_silent) {
        full_sys_prompt +=
            "\n\n【自主插话规则 — 必须极其克制】\n"
            "默认闭嘴。只有满足下面【任意一条】才可以开口，否则一律 [SILENT]：\n"
            "1. 点名塔菲/艾特塔菲，或明显在跟塔菲说话；\n"
            "2. 直接向塔菲提问、求助、要评价；\n"
            "3. 说话的人和你关系很近（见记忆：很熟的朋友/总找你玩/明确关系好的），且这条明显是说给你听的；\n"
            "4. 对方发图并期待你看图吐槽，且是在跟你分享（不是丢给别人的图）。\n"
            "以下情况必须 [SILENT]：\n"
            "- 别人之间闲聊、接龙、斗图、灌水、复读；\n"
            "- 只是路过的一两句话、表情、短语；\n"
            "- 普通群友（关系一般/不认识）的日常发言，即使有趣也不要接；\n"
            "- 群里已经很吵，不需要你刷存在感。\n"
            "若决定回复：禁止输出 [SILENT]，只说 1~2 句短句。\n"
            "若决定不回复：整条回复只输出 [SILENT]（不要任何其他字）。";
    }

    nlohmann::json sys_msg = nlohmann::json::object();
    sys_msg["role"] = "system";
    sys_msg["content"] = full_sys_prompt;
    messages.push_back(sys_msg);

    // 2. 携带最近短期消息
    for (const auto& msg : recent_msgs) {
        nlohmann::json m = nlohmann::json::object();
        m["role"] = msg.role;
        m["content"] = msg.content;
        messages.push_back(m);
    }

    // 3. 当前用户输入（支持多模态图片）
    std::vector<std::string> data_urls;
    for (const auto& p : image_paths) {
        std::string du = image_to_data_url(p, cfg_get()->auto_delete_images);
        if (!du.empty()) data_urls.push_back(du);
    }
    // 兜底：从文本里的 CQ:image 再抓一遍
    if (data_urls.empty()) {
        for (const auto& p : extract_image_refs(user_prompt)) {
            std::string du = image_to_data_url(p, cfg_get()->auto_delete_images);
            if (!du.empty()) data_urls.push_back(du);
        }
    }

    // 看完即删：清掉本条关联的本地图片（含未送视觉的）
    if (cfg_get()->auto_delete_images) {
        for (const auto& p : image_paths) {
            remove_local_image(p);
        }
        for (const auto& p : extract_image_refs(user_prompt)) {
            remove_local_image(p);
        }
    }

    nlohmann::json user_msg = nlohmann::json::object();
    user_msg["role"] = "user";

    std::string model_for_call = cfg_get()->model;
    bool has_vision = !data_urls.empty();
    if (has_vision) {
        // 有图 → 走视觉模型
        if (!cfg_get()->vision_model.empty()) {
            model_for_call = cfg_get()->vision_model;
        }
        nlohmann::json content = nlohmann::json::array();
        nlohmann::json text_part = nlohmann::json::object();
        text_part["type"] = std::string("text");
        std::string text_for_vision = user_prompt.empty() ? "请看图并用一句话说感受" : user_prompt;
        text_part["text"] = text_for_vision;
        content.push_back(text_part);
        for (const auto& du : data_urls) {
            // 标准 OpenAI 视觉格式：{"type":"image_url","image_url":{"url":"..."}}
            nlohmann::json img_part = nlohmann::json::object();
            img_part["type"] = std::string("image_url");
            nlohmann::json img_url = nlohmann::json::object();
            img_url["url"] = du;
            img_part["image_url"] = img_url;
            content.push_back(img_part);
        }
        user_msg["content"] = content;
    } else {
        user_msg["content"] = user_prompt;
    }
    messages.push_back(user_msg);

    // 识图请求给更多输出预算，避免被截断/空回复
    int tokens_for_call = cfg_get()->max_tokens;
    if (has_vision) {
        tokens_for_call = std::max(cfg_get()->max_tokens, 1500);
    }
    last_error_.clear();
    std::string reply = call_model(model_for_call, messages, tokens_for_call, cfg_get()->temperature);
    std::string vision_err = last_error_;

    // 视觉请求失败 → 用最简纯文本请求兜底（从零重建，不复用可能被污染的 messages）
    if (reply.empty() && has_vision) {
        nlohmann::json messages2 = nlohmann::json::array();
        nlohmann::json sys2 = nlohmann::json::object();
        sys2["role"] = std::string("system");
        sys2["content"] = full_sys_prompt;
        messages2.push_back(sys2);
        nlohmann::json user2 = nlohmann::json::object();
        user2["role"] = std::string("user");
        user2["content"] = user_prompt.empty() ? std::string("[对方发来一张图]") : (user_prompt + "（对方发了图）");
        messages2.push_back(user2);
        last_error_.clear();
        reply = call_model(cfg_get()->model, messages2, cfg_get()->max_tokens, cfg_get()->temperature);
        if (reply.empty() && last_error_.empty() && !vision_err.empty()) {
            last_error_ = vision_err;
        }
    }

    if (reply.empty()) {
        std::string detail = last_error_.empty() ? "未知错误" : last_error_;
        return "【AI 回复失败】" + detail;
    }

    // 自主插话：模型决定沉默
    if (allow_silent) {
        std::string trimmed = reply;
        // 去空白
        auto s = trimmed.find_first_not_of(" \t\r\n");
        auto e = trimmed.find_last_not_of(" \t\r\n");
        if (s == std::string::npos) return "";
        trimmed = trimmed.substr(s, e - s + 1);
        if (trimmed == "[SILENT]" || trimmed == "SILENT" || trimmed == "[SILENT]。") {
            return "";
        }
        // 含 [SILENT] 且几乎没别的内容
        if (trimmed.find("[SILENT]") != std::string::npos && utf8_length(trimmed) <= 10) {
            return "";
        }
        reply = trimmed;
    }

    // 更新短期记忆队列 (最多保留最近 2 轮；图片以占位符记入，省 token)
    {
        std::string mem_user = user_prompt;
        if (!image_paths.empty() || !data_urls.empty()) {
            mem_user += " [图片x" + std::to_string(data_urls.empty() ? image_paths.size() : data_urls.size()) + "]";
        }
        std::lock_guard<std::mutex> lock(memory_mutex_);
        auto& q = recent_sessions_[context_id];
        q.push_back({"user", mem_user});
        q.push_back({"assistant", reply});
        while (q.size() > 4) {
            q.pop_front();
        }
    }

    return reply;
}
