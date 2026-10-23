#pragma once
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>
#include "config.hpp"
#include "simple_json.hpp"

struct ChatMessage {
    std::string role;    // "system", "user", "assistant"
    std::string content;
};

class AIClient {
public:
    explicit AIClient(const AIConfig& config);
    ~AIClient();

    // 请求 AI 生成回复。image_paths 为本地图片绝对/相对路径（支持看图）
    // allow_silent=true 时允许模型输出 [SILENT] 表示不该插话，此时返回空串
    std::string chat(const std::string& context_id, const std::string& user_prompt,
                     const std::vector<std::string>& image_paths = {},
                     bool allow_silent = false);

    // 观察一条群聊/私聊消息，写入待简写缓冲（不阻塞、不调 API）
    // 缓冲攒够后自动丢给后台线程简写
    void observe(const std::string& context_id, const std::string& line);

    // 清除指定会话的长期记忆总结
    void clear_history(const std::string& context_id);

    // 持久化长期记忆总结至本地文件
    void save_summaries_to_disk();
    void load_summaries_from_disk();

    // 热更新配置（换服务商/换模型/换 key 后调用），立即对后续请求生效
    void update_config(const AIConfig& cfg);

    // 取一份加锁的配置快照指针。
    // 所有读取都必须走它：cfg_ 是 shared_ptr，update_config 会整体替换它，
    // 直接裸读 cfg_ 会和替换操作竞争（曾经导致 0xC0000409 崩溃）。
    std::shared_ptr<const AIConfig> cfg_get() const;

    // 取一份配置快照（需要一致视图时用）
    AIConfig snapshot_config() const;

    // 向服务端 GET /models，拉取当前 base_url + api_key 真正可用的模型列表。
    // 模型名迭代快、且各网关的"可用模型"取决于 key 的权限分组，
    // 硬编码列表必然过时 —— 让用户一键拉到真实列表才是可靠做法。
    // 返回模型名列表；失败时 err 里是 HTTP 状态与响应摘要。
    std::vector<std::string> fetch_models(std::string& err);

private:
    // 配置用 shared_ptr 持有：改配置时原子替换整个对象，
    // 正在跑的工作线程不受影响，也不需要加锁。
    // 之前这里是 AIConfig config_（值拷贝），改完配置后 QQ 那边永远用旧的
    // —— 「测试接口」用的是新配置所以能过，QQ 回复却还是旧配置报 401。
    std::shared_ptr<AIConfig> cfg_;
    mutable std::mutex cfg_mtx_;          // 保护 cfg_ 的替换
    std::string host_;
    std::string path_prefix_;
    bool is_ssl_ = false;

    // 长期记忆总结库: context_id -> 紧凑的长期记忆总结字符串 (不存原始聊天记录，永不膨胀)
    std::unordered_map<std::string, std::string> memory_summaries_;
    // 待简写的新消息缓冲
    std::unordered_map<std::string, std::deque<std::string>> memory_buffer_;
    // 仅保留最近短期上下文
    std::unordered_map<std::string, std::deque<ChatMessage>> recent_sessions_;
    std::mutex memory_mutex_;
    std::mutex chat_mutex_;

    // 后台记忆简写线程（绝不挡回复）
    std::thread mem_thread_;
    std::mutex mem_task_mutex_;
    std::condition_variable mem_cv_;
    std::deque<std::string> mem_pending_;          // 待简写的 context_id
    std::unordered_set<std::string> mem_queued_;   // 去重
    std::atomic<bool> mem_running_{true};

    void parse_base_url(const std::string& url);
    std::string send_http_request(const std::string& req_body);
    std::string call_model(const std::string& model, const nlohmann::json& messages,
                           int max_tokens, float temperature);
    std::string summarize_context(const std::string& context_id,
                                  const std::string& old_summary,
                                  const std::vector<std::string>& new_lines);
    void enqueue_summarize(const std::string& context_id);
    void mem_worker_loop();
    void do_summarize(const std::string& context_id);

    std::string last_error_;
};
