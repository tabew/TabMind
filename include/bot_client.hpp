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

    // 热更新配置（换服务商/换模型/换 key 之后调用）。
    // 之前 BotClient 构造时拷贝了一份配置，之后 UI 改什么都没用，
    // 表现就是「测试接口」能过、QQ 回复却还是旧配置报 401。
    void update_config(const AppConfig& config);

    // 转发 AI 侧的缓存命中统计（滑动窗口）
    AIClient::CacheStat cache_stat() const;

    // 发一条 QQ 空间说说（NapCat 扩展接口 send_qzone_msg）
    bool send_qzone(const std::string& content,
                    const std::vector<std::string>& images = {},
                    int ugc_right = 4);

    // 手动触发一次"发说说"（给 UI 上的按钮用）
    void post_qzone_now();
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

    // 每日 QQ 空间说说的定时线程
    std::thread qzone_thread_;
    std::atomic<bool> qzone_running_{true};
    void qzone_loop();
    bool qzone_already_posted_today() const;
    void mark_qzone_posted_today();
    simplews::WebSocketClient websocket_;
    bool is_running_ = false;

    void on_websocket_event(const simplews::Event& evt);
};
