#include "message_handler.hpp"
#include "plugin_manager.hpp"
#include "simple_json.hpp"
#include "simple_http.hpp"
#include "ui_window.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <thread>
#include <chrono>
#include <random>
#include <filesystem>

namespace fs = std::filesystem;

// 辅助函数：根据 url 下载图片到本地 data/images 目录并返回 file:// 协议本地 URI
static std::string download_image_to_local(const std::string& url_or_file) {
    if (url_or_file.empty()) return "";

    // 如果已经是本地有效路径
    if (fs::exists(url_or_file)) {
        std::string p = fs::absolute(url_or_file).generic_string();
        return "file:///" + p;
    }

    // 如果是 http/https 网络图片，立即下载到本地永久保存
    if (url_or_file.find("http://") == 0 || url_or_file.find("https://") == 0) {
        try {
            fs::create_directories("data/images");

            auto now = std::chrono::system_clock::now().time_since_epoch().count();
            std::string ext = ".jpg";
            if (url_or_file.find(".png") != std::string::npos) ext = ".png";
            else if (url_or_file.find(".gif") != std::string::npos) ext = ".gif";

            std::string filename = "img_" + std::to_string(now) + "_" + std::to_string(std::rand() % 10000) + ext;
            std::string local_path = "data/images/" + filename;

            auto resp = simplehttp::get(url_or_file);
            if (resp.status_code == 200 && !resp.body.empty()) {
                std::ofstream ofs(local_path, std::ios::binary);
                if (ofs.is_open()) {
                    ofs.write(resp.body.data(), resp.body.size());
                    ofs.close();
                    std::string p = fs::absolute(local_path).generic_string();
                    return "file:///" + p;
                }
            }
        } catch (...) {}
    }

    return url_or_file;
}

// 从 file:///... 或 data/images/... 提取真实本地路径
static std::string local_path_from_saved(const std::string& saved_path) {
    if (saved_path.empty()) return "";
    std::string p = saved_path;
    const std::string file_proto = "file:///";
    if (p.find(file_proto) == 0) {
        p = p.substr(file_proto.size());
    } else if (p.find("file://") == 0) {
        p = p.substr(7);
    }
    // 统一取出 data/images 段，便于相对定位
    size_t data_pos = p.find("data/images/");
    if (data_pos == std::string::npos) data_pos = p.find("data\\images\\");
    if (data_pos != std::string::npos) {
        std::string rel = p.substr(data_pos);
        std::replace(rel.begin(), rel.end(), '\\', '/');
        return rel;
    }
    return p;
}

// 删除本地下载的临时图片（看完/记完即删，省存储）
static void remove_local_image(const std::string& path_or_url) {
    if (path_or_url.empty()) return;
    if (path_or_url.find("http://") == 0 || path_or_url.find("https://") == 0 ||
        path_or_url.find("data:") == 0) {
        return;
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
    std::string base = path;
    size_t pos = base.find_last_of("/\\");
    if (pos != std::string::npos) base = base.substr(pos + 1);
    if (!base.empty()) {
        fs::remove("data/images/" + base, ec);
    }
}

static void remove_local_images(const std::vector<std::string>& paths) {
    for (const auto& p : paths) {
        remove_local_image(p);
    }
}

// 粗算 UTF-8 字数（用于水消息过滤）
static size_t utf8_len_approx(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

static std::vector<std::string> split_by_custom_delimiter(const std::string& full_text) {
    std::vector<std::string> result;
    if (full_text.empty()) return result;

    size_t start = 0;
    size_t pos = 0;
    std::vector<std::string> parts;

    while ((pos = full_text.find("//", start)) != std::string::npos) {
        std::string part = full_text.substr(start, pos - start);
        auto s = part.find_first_not_of(" \t\r\n");
        auto e = part.find_last_not_of(" \t\r\n");
        if (s != std::string::npos) {
            parts.push_back(part.substr(s, e - s + 1));
        }
        start = pos + 2;
    }

    std::string tail = full_text.substr(start);
    auto ts = tail.find_first_not_of(" \t\r\n");
    auto te = tail.find_last_not_of(" \t\r\n");
    if (ts != std::string::npos) {
        parts.push_back(tail.substr(ts, te - ts + 1));
    }

    if (parts.empty()) {
        return { full_text };
    }

    if (parts.size() == 1) {
        result.push_back(parts[0]);
    } else if (parts.size() == 2) {
        result.push_back(parts[0]);
        result.push_back(parts[1]);
    } else {
        result.push_back(parts[0]);
        std::string second_part;
        for (size_t i = 1; i < parts.size(); ++i) {
            second_part += (second_part.empty() ? "" : " ") + parts[i];
        }
        result.push_back(second_part);
    }

    return result;
}

MessageHandler::MessageHandler(const AppConfig& config, AIClient& ai_client, SendReplyCallback reply_cb,
                               SendFileCallback file_cb)
    : config_(config), ai_client_(ai_client), send_reply_(reply_cb), send_file_(file_cb), is_running_(true) {
    worker_thread_ = std::thread(&MessageHandler::worker_loop, this);
}

void MessageHandler::async_send_reply(const char* message_type, int64_t target_id, const char* text) {
    if (send_reply_ && message_type && text) {
        send_reply_(message_type, target_id, text);
    }
}

void MessageHandler::async_send_file(const char* message_type, int64_t target_id, const char* file_path, const char* display_name) {
    if (send_file_ && message_type && file_path) {
        send_file_(message_type, target_id, file_path, display_name ? display_name : "");
    }
}

void MessageHandler::async_log(const char* text) {
    if (text) MainWindow::get_instance().log(text);
}

void MessageHandler::fill_async_callbacks(PluginContext& pctx) {
    pctx.async_user_data = this;
    pctx.async_send_reply = [](void* ud, const char* mt, int64_t tid, const char* text) {
        static_cast<MessageHandler*>(ud)->async_send_reply(mt, tid, text);
    };
    pctx.async_send_file = [](void* ud, const char* mt, int64_t tid, const char* path, const char* name) {
        static_cast<MessageHandler*>(ud)->async_send_file(mt, tid, path, name);
    };
    pctx.async_log = [](void* ud, const char* text) {
        static_cast<MessageHandler*>(ud)->async_log(text);
    };
}

bool MessageHandler::is_close_friend(int64_t user_id) const {
    std::string id = std::to_string(user_id);
    for (const auto& f : config_.ai.close_friends) {
        if (f == id) return true;
    }
    return false;
}

bool MessageHandler::mentions_bot_name(const std::string& text) {
    static const char* keys[] = {
        "塔菲", "永雏塔菲", "Taffy", "taffy", "TAFFY", "菲菲", "小菲", "Ace Taffy"
    };
    for (const auto k : keys) {
        if (text.find(k) != std::string::npos) return true;
    }
    return false;
}

bool MessageHandler::reply_on_cooldown(const std::string& context_id) {
    std::lock_guard<std::mutex> lock(cooldown_mutex_);
    auto it = last_reply_time_.find(context_id);
    if (it == last_reply_time_.end()) return false;
    int cool = config_.ai.reply_cooldown_sec > 0 ? config_.ai.reply_cooldown_sec : 45;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - it->second).count();
    return elapsed < cool;
}

void MessageHandler::mark_replied(const std::string& context_id) {
    std::lock_guard<std::mutex> lock(cooldown_mutex_);
    last_reply_time_[context_id] = std::chrono::steady_clock::now();
}

MessageHandler::~MessageHandler() {
    is_running_ = false;
    queue_cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    // 退出前把剩余缓冲写入长期记忆
    try {
        ai_client_.save_summaries_to_disk();
    } catch (...) {}
}

void MessageHandler::worker_loop() {
    while (is_running_) {
        AITask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return !is_running_ || !task_queue_.empty();
            });

            if (!is_running_ && task_queue_.empty()) {
                break;
            }

            if (!task_queue_.empty()) {
                task = task_queue_.front();
                task_queue_.pop_front();
            } else {
                continue;
            }
        }

        try {
            process_ai_task(task);
        } catch (const std::exception& e) {
            std::string err_msg = "【AI 处理异常】" + std::string(e.what());
            MainWindow::get_instance().log("❌ " + err_msg);
            try {
                if (send_reply_ && task.force_reply) {
                    send_reply_(task.message_type, task.target_id, "呜呜...塔菲的大脑刚刚短路了喵（错误：" + std::string(e.what()) + "）");
                }
            } catch (...) {}
        } catch (...) {
            MainWindow::get_instance().log("❌ AI 处理遇到未知异常。");
        }
    }
}

void MessageHandler::process_ai_task(const AITask& task) {
    // 构造插件上下文
    PluginContext pctx;
    pctx.message_type = task.message_type;
    pctx.user_id = task.user_id;
    pctx.group_id = task.group_id;
    pctx.sender_name = task.sender_name;
    pctx.raw_message = task.raw_text;
    pctx.plain_text = task.prompt;
    pctx.is_at_bot = task.is_at_bot;
    pctx.send_reply = send_reply_;
    pctx.send_file = send_file_;
    pctx.log = [](const std::string& msg) { MainWindow::get_instance().log(msg); };
    fill_async_callbacks(pctx);

    // 调用 AI 生成回复；非强制时允许模型自主沉默
    bool allow_silent = !task.force_reply;
    std::string reply = ai_client_.chat(task.context_id, task.user_prompt_to_ai, task.image_paths, allow_silent);

    // 自主判断：决定不说话（用户消息已在 process_message_event 写入记忆）
    if (reply.empty()) {
        MainWindow::get_instance().log("💤 自主判断暂不回复 [" + task.message_type + " " + std::to_string(task.target_id) + "]");
        if (config_.ai.auto_delete_images) {
            remove_local_images(task.image_paths);
        }
        return;
    }

    if (send_reply_) {
        mark_replied(task.context_id);
        // AI 回复后置钩子
        PluginManager::get_instance().dispatch_ai_reply(pctx, reply);

        bool need_quote = false;
        if (reply.find("[REPLY]") != std::string::npos) {
            need_quote = true;
            size_t rpos;
            while ((rpos = reply.find("[REPLY]")) != std::string::npos) {
                reply.erase(rpos, 7);
            }
        }

        std::vector<std::string> messages_to_send = split_by_custom_delimiter(reply);

        for (size_t i = 0; i < messages_to_send.size(); ++i) {
            std::string msg = messages_to_send[i];

            if (i == 0 && need_quote && task.message_id != 0 && msg.find("[CQ:reply,") == std::string::npos) {
                msg = "[CQ:reply,id=" + std::to_string(task.message_id) + "]" + msg;
            }

            send_reply_(task.message_type, task.target_id, msg);

            if (i + 1 < messages_to_send.size()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(750));
            }
        }
    }

    // 只把 AI 自己的回复补进记忆（用户消息已记过）
    {
        if (config_.ai.auto_delete_images) {
            remove_local_images(task.image_paths);
        }
        ai_client_.observe(task.context_id, "【我】: " + reply);
    }
}

void MessageHandler::handle_raw_event(const std::string& payload) {
    try {
        nlohmann::json event = nlohmann::json::parse(payload);
        if (!event.contains("post_type")) {
            return;
        }

        std::string post_type = event["post_type"].get<std::string>();
        if (post_type == "message") {
            process_message_event(event);
        }
    } catch (const std::exception& e) {
        std::cerr << "[MessageHandler] 事件解析错误: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[MessageHandler] 事件解析遇到未知错误" << std::endl;
    }
}

std::string MessageHandler::extract_plain_text_and_check_at(const nlohmann::json& message_field, bool& is_at_bot,
                                                            std::vector<std::string>& image_paths) {
    is_at_bot = false;
    std::string text_result;
    image_paths.clear();

    if (message_field.is_string()) {
        text_result = message_field.get<std::string>();
        std::string at_str = "[CQ:at,qq=" + std::to_string(config_.onebot.bot_qq) + "]";
        if (text_result.find(at_str) != std::string::npos) {
            is_at_bot = true;
            size_t pos;
            while ((pos = text_result.find(at_str)) != std::string::npos) {
                text_result.erase(pos, at_str.length());
            }
        }
        // 字符串消息里的 CQ:image
        size_t ipos = 0;
        const std::string ikey = "[CQ:image,file=";
        while ((ipos = text_result.find(ikey, ipos)) != std::string::npos) {
            size_t s = ipos + ikey.size();
            size_t e = text_result.find(']', s);
            if (e == std::string::npos) break;
            std::string val = text_result.substr(s, e - s);
            size_t comma = val.find(',');
            if (comma != std::string::npos) val = val.substr(0, comma);
            std::string lp = local_path_from_saved(val);
            if (!lp.empty()) image_paths.push_back(lp);
            ipos = e + 1;
        }
    } else if (message_field.is_array()) {
        for (size_t i = 0; i < message_field.size(); ++i) {
            const auto& seg = message_field[i];
            if (seg.contains("type")) {
                std::string type = seg["type"].get<std::string>();
                if (type == "text" && seg.contains("data") && seg["data"].contains("text")) {
                    text_result += seg["data"]["text"].get<std::string>();
                } else if (type == "image" && seg.contains("data")) {
                    std::string img_target;
                    if (seg["data"].contains("url") && !seg["data"]["url"].get<std::string>().empty()) {
                        img_target = seg["data"]["url"].get<std::string>();
                    } else if (seg["data"].contains("file") && !seg["data"]["file"].get<std::string>().empty()) {
                        img_target = seg["data"]["file"].get<std::string>();
                    }

                    if (!img_target.empty()) {
                        std::string saved_path = download_image_to_local(img_target);
                        if (!saved_path.empty()) {
                            // 优先提取 data/images/xxx.ext 相对路径，确保部署到服务器或其他盘符路径时仍能正确寻址
                            size_t data_pos = saved_path.find("data/images/");
                            if (data_pos == std::string::npos) {
                                data_pos = saved_path.find("data\\images\\");
                            }
                            if (data_pos != std::string::npos) {
                                std::string rel = saved_path.substr(data_pos);
                                std::replace(rel.begin(), rel.end(), '\\', '/');
                                text_result += "[CQ:image,file=" + rel + "]";
                                image_paths.push_back(rel);
                            } else {
                                text_result += "[CQ:image,file=" + saved_path + "]";
                                std::string lp = local_path_from_saved(saved_path);
                                if (!lp.empty()) image_paths.push_back(lp);
                            }
                        }
                    }
                } else if (type == "face" && seg.contains("data") && seg["data"].contains("id")) {
                    std::string face_id = seg["data"]["id"].is_string() ? seg["data"]["id"].get<std::string>() : std::to_string(seg["data"]["id"].get<int>());
                    text_result += "[CQ:face,id=" + face_id + "]";
                } else if (type == "at" && seg.contains("data") && seg["data"].contains("qq")) {
                    std::string qq_str;
                    const auto& qq_val = seg["data"]["qq"];
                    if (qq_val.is_string()) {
                        qq_str = qq_val.get<std::string>();
                    } else if (qq_val.is_number()) {
                        qq_str = std::to_string(qq_val.get<int64_t>());
                    }
                    if (qq_str == std::to_string(config_.onebot.bot_qq) || qq_str == "all") {
                        is_at_bot = true;
                    }
                }
            }
        }
    }

    auto start = text_result.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = text_result.find_last_not_of(" \t\n\r");
    return text_result.substr(start, end - start + 1);
}

std::string MessageHandler::clean_trigger_prefix(const std::string& text, bool& matched) {
    matched = false;
    if (config_.onebot.trigger_prefixes.empty()) {
        return text;
    }

    for (const auto& prefix : config_.onebot.trigger_prefixes) {
        if (text.find(prefix) == 0) {
            matched = true;
            std::string cleaned = text.substr(prefix.length());
            auto start = cleaned.find_first_not_of(" \t\n\r");
            if (start == std::string::npos) {
                return text;
            }
            return cleaned.substr(start);
        }
    }
    return text;
}

// 判断插件是否在群聊免@白名单（按 DLL 文件名或插件显示名子串匹配，忽略大小写）
static bool is_at_exempt_plugin(const PluginInfo& info, const std::vector<std::string>& exempt_list) {
    auto to_lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    };
    // DLL 文件名（不含扩展名）+ 插件显示名，均参与匹配
    std::string dll_name = info.file_path;
    auto slash = dll_name.find_last_of("/\\");
    if (slash != std::string::npos) dll_name = dll_name.substr(slash + 1);
    auto dot = dll_name.find_last_of('.');
    if (dot != std::string::npos) dll_name = dll_name.substr(0, dot);

    std::string hay = to_lower(dll_name + " " + info.name + " " + info.description);
    for (const auto& raw : exempt_list) {
        std::string key = to_lower(raw);
        auto s = key.find_first_not_of(" \t\r\n");
        auto e = key.find_last_not_of(" \t\r\n");
        if (s == std::string::npos) continue;
        key = key.substr(s, e - s + 1);
        if (!key.empty() && hay.find(key) != std::string::npos) return true;
    }
    return false;
}

void MessageHandler::process_message_event(const nlohmann::json& event) {
    std::string message_type = event["message_type"].get<std::string>();
    int64_t user_id = event["user_id"].get<int64_t>();
    int64_t message_id = event.contains("message_id") ? event["message_id"].get<int64_t>() : 0;
    int64_t group_id = (message_type == "group" && event.contains("group_id")) ? event["group_id"].get<int64_t>() : 0;

    // 忽略 Bot 自己发送的消息
    if (user_id == config_.onebot.bot_qq) {
        return;
    }

    // 解析发送者昵称 / 群名片
    std::string sender_name = std::to_string(user_id);
    if (event.contains("sender")) {
        const auto& sender = event["sender"];
        if (sender.contains("card") && !sender["card"].get<std::string>().empty()) {
            sender_name = sender["card"].get<std::string>();
        } else if (sender.contains("nickname") && !sender["nickname"].get<std::string>().empty()) {
            sender_name = sender["nickname"].get<std::string>();
        }
    }

    bool is_at_bot = false;
    std::vector<std::string> image_paths;
    std::string raw_text = extract_plain_text_and_check_at(event["message"], is_at_bot, image_paths);

    bool prefix_matched = false;
    std::string prompt = clean_trigger_prefix(raw_text, prefix_matched);

    std::vector<int64_t> at_user_ids;
    if (event.contains("message") && event["message"].is_array()) {
        for (const auto& seg : event["message"]) {
            if (seg.contains("type") && seg["type"].get<std::string>() == "at" && seg.contains("data") && seg["data"].contains("qq")) {
                const auto& qq_val = seg["data"]["qq"];
                int64_t parsed_qq = 0;
                if (qq_val.is_number()) {
                    parsed_qq = qq_val.get<int64_t>();
                } else if (qq_val.is_string()) {
                    std::string s = qq_val.get<std::string>();
                    if (s != "all") {
                        try { parsed_qq = std::stoll(s); } catch (...) {}
                    }
                }
                if (parsed_qq > 0) {
                    at_user_ids.push_back(parsed_qq);
                }
            }
        }
    }

    // =========================================================================
    // 【C++ 外部扩展插件分发处理】（同步分发）
    // =========================================================================
    PluginContext pctx;
    pctx.message_type = message_type;
    pctx.user_id = user_id;
    pctx.group_id = group_id;
    pctx.sender_name = sender_name;
    pctx.at_user_ids = at_user_ids;
    pctx.raw_message = raw_text;
    pctx.plain_text = prompt;
    pctx.is_at_bot = is_at_bot;
    pctx.send_reply = send_reply_;
    pctx.send_file = send_file_;
    pctx.log = [](const std::string& msg) { MainWindow::get_instance().log(msg); };
    fill_async_callbacks(pctx);

    // =========================================================================
    // 【群聊 @ 门禁】开启时：未@且未命中触发前缀的群消息只放行免@白名单插件
    // =========================================================================
    const bool group_gated = (message_type == "group") && config_.onebot.group_require_at
                             && !is_at_bot && !prefix_matched;

    bool plugin_handled = false;
    if (group_gated) {
        plugin_handled = PluginManager::get_instance().dispatch_message(pctx,
            [this](const PluginInfo& info) {
                return is_at_exempt_plugin(info, config_.onebot.at_exempt_plugins);
            });
    } else {
        plugin_handled = PluginManager::get_instance().dispatch_message(pctx);
    }

    if (plugin_handled) {
        return; // 插件拦截处理
    }

    std::string context_id;
    int64_t target_id = 0;
    std::string user_prompt_to_ai;
    bool force_reply = true;

    if (message_type == "private") {
        target_id = user_id;
        context_id = "private_" + std::to_string(user_id);

        if (prompt == "/reset" || prompt == "重置" || prompt == "清除记忆" || prompt == "/clear" || prompt == "清空记忆") {
            if (config_.ai.auto_delete_images) {
                remove_local_images(image_paths);
            }
            ai_client_.clear_history(context_id);
            if (send_reply_) send_reply_("private", target_id, "已清除您的长期记忆总结。");
            return;
        }

        // 私聊消息全量入记忆（observe 内部异步简写）
        {
            std::string mem_line = "【" + sender_name + "】: " + (raw_text.empty() ? (image_paths.empty() ? "（空）" : "[图片]") : raw_text);
            ai_client_.observe(context_id, mem_line);
        }

        user_prompt_to_ai = "【好友 " + sender_name + "(QQ:" + std::to_string(user_id) + ", msg_id:" + std::to_string(message_id) + ")】: " + prompt;
        // 纯图片私聊也要能看
        if (prompt.empty() && !image_paths.empty()) {
            user_prompt_to_ai = "【好友 " + sender_name + "(QQ:" + std::to_string(user_id) + ", msg_id:" + std::to_string(message_id) + ")】: [发来 " + std::to_string(image_paths.size()) + " 张图片]";
        }
        std::cout << "[私聊] " << sender_name << " (" << user_id << "): " << prompt << std::endl;
    } else if (message_type == "group") {
        target_id = group_id;
        context_id = "group_" + std::to_string(group_id);

        // ---------- 群聊全量消息一律写入记忆（即使不回复；简写在后台） ----------
        {
            std::string mem_line = "【" + sender_name + "】: " + (raw_text.empty() ? (image_paths.empty() ? "（空）" : "[图片]") : raw_text);
            ai_client_.observe(context_id, mem_line);
        }

        // 群聊触发条件：
        //  - 开启「必须@才响应」：仅 @机器人 或命中触发前缀才回复（免@插件已在上面放行）
        //  - 关闭门禁：未@消息交给模型自主判断是否插话（可输出沉默），并受插话冷却约束
        if (is_at_bot || prefix_matched) {
            force_reply = true;
        } else if (config_.onebot.group_require_at) {
            // 必须@模式：其它一律不回（已写入记忆）
            if (config_.ai.auto_delete_images) {
                remove_local_images(image_paths);
            }
            return;
        } else {
            // 宽松模式：自主判断 + 冷却防话痨
            if (reply_on_cooldown(context_id)) {
                if (config_.ai.auto_delete_images) {
                    remove_local_images(image_paths);
                }
                return;
            }
            force_reply = false;
        }

        if (prompt == "/reset" || prompt == "重置" || prompt == "清除记忆" || prompt == "/clear" || prompt == "清空记忆") {
            if (config_.ai.auto_delete_images) {
                remove_local_images(image_paths);
            }
            ai_client_.clear_history(context_id);
            if (send_reply_) send_reply_("group", target_id, "已清除本群的长期记忆总结。");
            return;
        }

        user_prompt_to_ai = "【群成员 " + sender_name + "(QQ:" + std::to_string(user_id) + ", msg_id:" + std::to_string(message_id) + ")】: " + prompt;
        if (prompt.empty() && !image_paths.empty()) {
            user_prompt_to_ai = "【群成员 " + sender_name + "(QQ:" + std::to_string(user_id) + ", msg_id:" + std::to_string(message_id) + ")】: [发来 " + std::to_string(image_paths.size()) + " 张图片，请看图]";
        }
        std::cout << "[群聊] 来自群 " << group_id << " [" << sender_name << " (" << user_id << ")]: " << prompt
                  << (image_paths.empty() ? "" : " (+图)") << std::endl;
    } else {
        return;
    }

    // 完全空消息且无图：不进 AI
    if (prompt.empty() && image_paths.empty()) {
        return;
    }

    // 压入 AI 异步任务队列
    AITask task;
    task.message_type = message_type;
    task.target_id = target_id;
    task.user_id = user_id;
    task.group_id = group_id;
    task.message_id = message_id;
    task.sender_name = sender_name;
    task.raw_text = raw_text;
    task.prompt = prompt;
    task.context_id = context_id;
    task.user_prompt_to_ai = user_prompt_to_ai;
    task.is_at_bot = is_at_bot;
    task.force_reply = force_reply;
    task.image_paths = image_paths;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push_back(task);
    }
    queue_cv_.notify_one();
}
