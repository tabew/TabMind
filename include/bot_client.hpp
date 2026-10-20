#pragma once
#include <string>
#include <memory>
#include "simple_ws.hpp"
#include "config.hpp"
#include "ai_client.hpp"
#include "message_handler.hpp"

class BotClient {
public:
    explicit BotClient(const AppConfig& config);
    ~BotClient();

    // 启动 WebSocket 连接
    void start();

    // 停止运行
    void stop();

    // 发送消息 API
    void send_message(const std::string& message_type, int64_t target_id, const std::string& text);

    // 发送本地文件 API（PDF/图片等），走 OneBot upload_group_file / upload_private_file
    void send_file(const std::string& message_type, int64_t target_id,
                   const std::string& file_path, const std::string& display_name);

private:
    AppConfig config_;
    AIClient ai_client_;
    std::unique_ptr<MessageHandler> msg_handler_;
    simplews::WebSocketClient websocket_;
    bool is_running_ = false;

    void on_websocket_event(const simplews::Event& evt);
};
