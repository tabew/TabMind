#include "bot_client.hpp"
#include "ui_core.hpp"
#include "ui_window.hpp"
#include "simple_json.hpp"
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

// 辅助函数：将消息中的 CQ:image 相对路径动态转换为基于当前运行目录的绝对 file:/// 协议路径
static std::string transform_cq_image_paths(const std::string& text) {
    std::string result = text;
    size_t pos = 0;
    while ((pos = result.find("[CQ:image,file=", pos)) != std::string::npos) {
        size_t file_start = pos + 15; // length of "[CQ:image,file="
        size_t end_pos = result.find("]", file_start);
        if (end_pos == std::string::npos) break;

        std::string file_val = result.substr(file_start, end_pos - file_start);
        // 如果是本地相对路径 (且不是 http://, https://, base64://, file:///)
        if (file_val.find("http://") != 0 && file_val.find("https://") != 0 &&
            file_val.find("base64://") != 0 && file_val.find("file://") != 0) {
            
            // 检查本地文件是否存在或转为当前运行目录下的绝对路径
            if (fs::exists(file_val)) {
                std::string abs_path = fs::absolute(file_val).generic_string();
                std::string replacement = "file:///" + abs_path;
                result.replace(file_start, file_val.length(), replacement);
                pos = file_start + replacement.length() + 1;
                continue;
            }
        }
        pos = end_pos + 1;
    }
    return result;
}

static void log_info(const std::string& msg) {
    std::cout << msg << std::endl;
    file_log(msg);                       // 服务器上没有控制台，靠文件看日志
    MainWindow::get_instance().log(msg);
}

BotClient::BotClient(const AppConfig& config)
    : config_(config), ai_client_(config.ai) {
    
    // 启动每日说说定时器
    qzone_thread_ = std::thread(&BotClient::qzone_loop, this);

    // 初始化消息处理器并绑定发送消息/文件回调
    msg_handler_ = std::make_unique<MessageHandler>(
        config_,
        ai_client_,
        [this](const std::string& type, int64_t id, const std::string& reply) {
            this->send_message(type, id, reply);
        },
        [this](const std::string& type, int64_t id, const std::string& path, const std::string& name) {
            this->send_file(type, id, path, name);
        }
    );

    websocket_.set_callback([this](const simplews::Event& evt) {
        this->on_websocket_event(evt);
    });
}

BotClient::~BotClient() {
    stop();
}

// 热更新配置：把新的 ai 配置推到 AIClient，后续请求立即使用新配置。
// OneBot 的 ws_url 若变了则需要重连，这里只处理 AI 侧（换服务商/换模型/换 key）。

// ---------------------------------------------------------------------------
// 每日 QQ 空间说说
//
// 到点综合长期记忆，写一条"像真人发朋友圈"的说说，通过 NapCat 的
// send_qzone_msg 扩展接口发出去。一天只发一次（用 data/qzone_last.txt 记日期）。
// ---------------------------------------------------------------------------
static std::string qzone_stamp_path() { return "data/qzone_last.txt"; }

static std::string today_str() {
    std::time_t tt = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &tt);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

bool BotClient::qzone_already_posted_today() const {
    std::ifstream ifs(qzone_stamp_path());
    if (!ifs.is_open()) return false;
    std::string d;
    ifs >> d;
    return d == today_str();
}

void BotClient::mark_qzone_posted_today() {
    try {
        fs::create_directories("data");
        std::ofstream ofs(qzone_stamp_path(), std::ios::trunc);
        ofs << today_str();
    } catch (...) {}
}

bool BotClient::send_qzone(const std::string& content,
                           const std::vector<std::string>& images,
                           int ugc_right) {
    if (content.empty()) return false;
    nlohmann::json req;
    req["action"] = "send_qzone_msg";
    req["params"] = nlohmann::json::object();
    req["params"]["content"] = content;
    nlohmann::json imgs = nlohmann::json::array();
    for (const auto& im : images) imgs.push_back(im);
    req["params"]["images"] = imgs;
    req["params"]["ugc_right"] = ugc_right;
    nlohmann::json t = nlohmann::json::array();
    req["params"]["target_uins"] = t;

    bool ok = websocket_.send_text(req.dump());
    if (ok) {
        std::string preview = content.substr(0, 30) + (content.size() > 30 ? "..." : "");
        log_info("[Qzone] 说说已发送: " + preview);
        file_log("[Qzone] 说说已发送 (" + std::to_string(content.size()) + " 字): " + preview);
    } else {
        log_info("[Qzone] 发送失败（WebSocket 未连接？）");
        file_log("[Qzone] 发送失败: WebSocket 未连接或发送被拒");
    }
    return ok;
}

// 手动/定时触发一次"发说说"：先用记忆生成内容，再发出去
void BotClient::post_qzone_now() {
    std::string memory = ai_client_.all_memory_summary();
    std::string prompt;
    if (memory.empty()) {
        prompt =
            "你现在要发一条 QQ 空间说说。\n"
            "写一条鲸鱼娘的日常碎碎念：想吃白饭、犯懒、吐槽天气、感慨今天之类的。\n"
            "50~100 字，像真人随手发的，有情绪有细节。\n"
            "不要提记忆/AI/模型/系统，不要 emoji，不要分段，不要标题。";
    } else {
        prompt =
            "你现在要发一条 QQ 空间说说，综合下面这些记忆来写。\n\n"
            "===== 长期记忆 =====\n" + memory + "\n"
            "===== 要求 =====\n"
            "1. 以第一人称，像真人随手发的朋友圈，不要像新闻稿/总结/公告；\n"
            "2. 有情绪、有细节：可以吐槽、感慨、记录日常、提某件具体的事；\n"
            "3. 50~100 字；\n"
            "4. 绝不出现「记忆」「AI」「模型」「系统」「用户」这些词；\n"
            "5. 不用 emoji，不分段，不带标题，不以「今天又」开头；\n"
            "6. 像鲸鱼娘这个人写的：有点懒、嘴馋、偶尔傲娇。";
    }

    log_info("[Qzone] 正在生成说说内容...");
    file_log("[Qzone] 开始生成每日说说（记忆片段数: " + std::to_string(memory.empty() ? 0 : 1) + "）");
    std::string content = ai_client_.generate_text(prompt, 0.95f);
    if (content.empty()) {
        log_info("[Qzone] 生成失败，跳过本次");
        file_log("[Qzone] 生成失败: " + ai_client_.last_error());
        return;
    }
    // 粗暴去模型偶尔加的引号/前缀
    while (!content.empty() && (content.front() == ' ' || content.front() == '\n' ||
                                content.front() == '"'  || content.front() == '\u300c')) content.erase(content.begin());
    while (!content.empty() && (content.back() == ' '  || content.back() == '\n' ||
                                content.back() == '"'  || content.back() == '\u300d')) content.pop_back();
    send_qzone(content, {}, config_.onebot.qzone_ugc_right);
}

// 定时线程：到点自动发
void BotClient::qzone_loop() {
    log_info("[Qzone] 每日说说定时器已启动（开关: " +
             std::string(config_.onebot.qzone_daily ? "开" : "关") +
             "，时间 " + std::to_string(config_.onebot.qzone_hour) + ":" +
             std::to_string(config_.onebot.qzone_minute) + "）");
    while (qzone_running_) {
        for (int i = 0; i < 60 && qzone_running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!qzone_running_ || !config_.onebot.qzone_daily) continue;
        std::time_t tt = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &tt);
        if (tm.tm_hour != config_.onebot.qzone_hour) continue;
        if (tm.tm_min != config_.onebot.qzone_minute) continue;
        if (qzone_already_posted_today()) {
            log_info("[Qzone] 今天已发过说说，跳过");
            continue;
        }
        post_qzone_now();
        mark_qzone_posted_today();
    }
}

AIClient::CacheStat BotClient::cache_stat() const {
    return ai_client_.cache_stat();
}

void BotClient::update_config(const AppConfig& config) {
    config_ = config;
    ai_client_.update_config(config.ai);
    log_info("[BotClient] 配置已热更新: base_url=" + config.ai.base_url +
             " model=" + config.ai.model);
}

void BotClient::start() {
    std::string url = config_.onebot.ws_url;
    if (!config_.onebot.access_token.empty()) {
        url += (url.find('?') == std::string::npos ? "?" : "&");
        url += "access_token=" + config_.onebot.access_token;
    }

    log_info("[BotClient] 正在连接 OneBot 服务: " + config_.onebot.ws_url + " ...");
    websocket_.connect(url);
    is_running_ = true;
}

void BotClient::stop() {
    qzone_running_ = false;
    if (qzone_thread_.joinable()) qzone_thread_.join();
    if (is_running_) {
        websocket_.stop();
        is_running_ = false;
        log_info("[BotClient] 已断开连接并停止服务。");
    }
}

void BotClient::on_websocket_event(const simplews::Event& evt) {
    try {
        if (evt.type == simplews::EventType::Message) {
            // 交由 MessageHandler 解析 OneBot 协议事件
            msg_handler_->handle_raw_event(evt.message);
        } else if (evt.type == simplews::EventType::Open) {
            log_info("[BotClient] √ 成功连接至 OneBot WebSocket 服务端！");
        } else if (evt.type == simplews::EventType::Close) {
            log_info("[BotClient] ! 与 OneBot 的连接已断开，正在准备重连...");
        } else if (evt.type == simplews::EventType::Error) {
            log_info("[BotClient] × WebSocket 提示: " + evt.error);
        }
    } catch (const std::exception& e) {
        log_info(std::string("[BotClient] 处理 WebSocket 事件异常: ") + e.what());
    } catch (...) {
        log_info("[BotClient] 处理 WebSocket 事件遇到未知异常");
    }
}

void BotClient::send_message(const std::string& message_type, int64_t target_id, const std::string& text) {
    std::string converted_text = transform_cq_image_paths(text);

    nlohmann::json req;
    req["action"] = "send_msg";
    req["params"] = nlohmann::json::object();
    req["params"]["message_type"] = message_type;
    req["params"][message_type == "group" ? "group_id" : "user_id"] = target_id;
    req["params"]["message"] = converted_text;

    std::string payload = req.dump();
    bool ok = websocket_.send_text(payload);
    if (!ok) {
        log_info("[BotClient] 发送消息失败: " + converted_text);
    } else {
        std::string preview = converted_text.substr(0, std::min<size_t>(converted_text.size(), 40)) + (converted_text.size() > 40 ? "..." : "");
        log_info("[BotClient] -> 回复成功 [" + message_type + " " + std::to_string(target_id) + "]: " + preview);
    }
}

void BotClient::send_file(const std::string& message_type, int64_t target_id,
                          const std::string& file_path, const std::string& display_name) {
    if (!fs::exists(file_path)) {
        log_info("[BotClient] 发送文件失败，文件不存在: " + file_path);
        return;
    }

    // 将本地路径转换为 file:/// 绝对路径，供 NapCat / OneBot 识别
    std::string abs_path = fs::absolute(file_path).generic_string();
    std::string file_uri = "file:///" + abs_path;
    std::string name = display_name.empty() ? fs::path(file_path).filename().string() : display_name;

    nlohmann::json req;
    req["params"] = nlohmann::json::object();
    if (message_type == "group") {
        req["action"] = "upload_group_file";
        req["params"]["group_id"] = target_id;
    } else {
        req["action"] = "upload_private_file";
        req["params"]["user_id"] = target_id;
    }
    req["params"]["file"] = file_uri;
    req["params"]["name"] = name;

    std::string payload = req.dump();
    bool ok = websocket_.send_text(payload);
    if (!ok) {
        log_info("[BotClient] 发送文件失败: " + abs_path);
    } else {
        log_info("[BotClient] -> 文件已提交上传 [" + message_type + " " + std::to_string(target_id) + "]: " + name +
                 " (" + std::to_string(fs::file_size(file_path) / 1024) + " KB)");
    }
}
