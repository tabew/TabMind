#pragma once
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <chrono>
#include "simple_json.hpp"
#include "config.hpp"
#include "ai_client.hpp"

struct PluginContext;

// 回调函数类型：用于将生成的回复消息发送出去
using SendReplyCallback = std::function<void(const std::string& message_type, int64_t target_id, const std::string& reply_text)>;
// 回调函数类型：用于将本地文件上传并发送给用户/群
using SendFileCallback = std::function<void(const std::string& message_type, int64_t target_id, const std::string& file_path, const std::string& display_name)>;

struct AITask {
    std::string message_type;
    int64_t target_id = 0;
    int64_t user_id = 0;
    int64_t group_id = 0;
    int64_t message_id = 0;
    std::string sender_name;
    std::string raw_text;
    std::string prompt;
    std::string context_id;
    std::string user_prompt_to_ai;
    bool is_at_bot = false;
    // true = 被@/前缀/私聊，必须回复；false = 自主判断（可输出沉默）
    bool force_reply = true;
    // 本条消息里的本地图片路径（供视觉模型看图）
    std::vector<std::string> image_paths;
};

class MessageHandler {
public:
    MessageHandler(const AppConfig& config, AIClient& ai_client, SendReplyCallback reply_cb,
                   SendFileCallback file_cb = nullptr);
    ~MessageHandler();

    // 解析并处理接收到的 OneBot WebSocket 原始 JSON 文本
    void handle_raw_event(const std::string& payload);

private:
    AppConfig config_;
    AIClient& ai_client_;
    SendReplyCallback send_reply_;
    SendFileCallback send_file_;

    std::deque<AITask> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> is_running_{true};
    std::thread worker_thread_;
    // 每个会话上次主动插话时间（冷却用）
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_reply_time_;
    std::mutex cooldown_mutex_;

    void worker_loop();
    void process_ai_task(const AITask& task);
    void process_message_event(const nlohmann::json& event);
    void fill_async_callbacks(PluginContext& pctx);
    // 供插件后台线程安全调用
    void async_send_reply(const char* message_type, int64_t target_id, const char* text);
    void async_send_file(const char* message_type, int64_t target_id, const char* file_path, const char* display_name);
    void async_log(const char* text);
    std::string extract_plain_text_and_check_at(const nlohmann::json& message_field, bool& is_at_bot,
                                                std::vector<std::string>& image_paths);
    std::string clean_trigger_prefix(const std::string& text, bool& matched);
    bool is_close_friend(int64_t user_id) const;
    static bool mentions_bot_name(const std::string& text);
    bool reply_on_cooldown(const std::string& context_id);
    void mark_replied(const std::string& context_id);
};
