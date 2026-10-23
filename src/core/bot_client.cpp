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
