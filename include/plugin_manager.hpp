#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <windows.h>
#include "simple_json.hpp"

// 线程安全的 C 回调（插件可保存到后台线程调用；宿主保证任意线程安全）
using PluginAsyncSendReplyFn = void(*)(void* user_data, const char* message_type, int64_t target_id, const char* text);
using PluginAsyncSendFileFn = void(*)(void* user_data, const char* message_type, int64_t target_id, const char* file_path, const char* display_name);
using PluginAsyncLogFn = void(*)(void* user_data, const char* text);

// 插件上下文，传递给插件调用
struct PluginContext {
    std::string message_type; // "private" 或 "group"
    int64_t user_id = 0;
    int64_t group_id = 0;
    std::string sender_name;  // 发送者昵称 / 群名片
    std::vector<int64_t> at_user_ids; // 被 @ 的用户 QQ 号
    std::string raw_message;  // 原始接收文本 (如包含 [CQ:image,file=...] 或 CQ码)
    std::string plain_text;   // 剥离后的纯文本
    bool is_at_bot = false;

    // 发送消息回调 (message_type, target_id, reply_text)
    // 仅可在 on_message 同步调用期间使用，不要复制到后台线程
    std::function<void(const std::string&, int64_t, const std::string&)> send_reply;
    // 输出日志到 UI（同样仅同步使用）
    std::function<void(const std::string&)> log;
    // 发送文件回调 (message_type, target_id, file_path, display_name)
    // 必须放在末尾，保持旧插件对前序字段的 ABI 兼容
    std::function<void(const std::string&, int64_t, const std::string&, const std::string&)> send_file;

    // ---- 线程安全 C API：后台任务请用这一组，可保存指针稍后调用 ----
    void* async_user_data = nullptr;
    PluginAsyncSendReplyFn async_send_reply = nullptr;
    PluginAsyncSendFileFn async_send_file = nullptr;
    PluginAsyncLogFn async_log = nullptr;
};

// 插件信息元数据
struct PluginInfo {
    std::string name;        // 插件名称
    std::string version;     // 插件版本
    std::string author;      // 作者
    std::string description; // 简介描述
    std::string file_path;   // DLL 文件路径
    bool enabled = true;     // 是否启用
};

// 插件基类接口 (用于 C++ 动态链接库 DLL)
class IBotPlugin {
public:
    virtual ~IBotPlugin() = default;
    virtual PluginInfo get_info() = 0;
    virtual bool on_load() = 0;
    virtual void on_unload() = 0;
    
    // 1. 消息前置处理：
    // 返回 true 表示插件已拦截处理该消息（无需再调用大模型 AI）
    // 返回 false 表示插件未拦截，继续交给 AI 思考回复
    virtual bool on_message(const PluginContext& ctx) = 0;

    // 2. AI 回复后置处理钩子：
    // 当 AI 思考生成了 ai_reply_text，在发送给 QQ 用户之前会经过此钩子
    // 插件可以对文本进行修饰、追加表情包 [CQ:image,file=...] 等
    virtual void on_ai_reply(const PluginContext& ctx, std::string& ai_reply_text) {}
};

// 导出的 C 风格 DLL 接口函数定义
typedef IBotPlugin* (*CreatePluginFunc)();
typedef void (*DestroyPluginFunc)(IBotPlugin*);

// 插件管理系统
class PluginManager {
public:
    static PluginManager& get_instance();

    // 扫描 plugins 目录并自动加载所有 .dll 插件
    void load_plugins(const std::string& plugin_dir = "plugins");
    
    // 卸载所有插件
    void unload_all();

    // 广播消息给所有已启用的插件
    // 如果某个插件返回 true，说明已被插件处理，主程序应跳过 AI 回复
    // allow_filter 非空时，仅派发给过滤器返回 true 的插件（用于群聊免@白名单）
    bool dispatch_message(const PluginContext& ctx,
                          const std::function<bool(const PluginInfo&)>& allow_filter = nullptr);

    // AI 生成回复后的后置钩子（供表情包、敏感词过滤等插件修改回复内容）
    void dispatch_ai_reply(const PluginContext& ctx, std::string& ai_reply_text);

    // 获取所有已加载的插件列表（供 UI 展示）
    std::vector<PluginInfo> get_plugin_list() const;

    // 切换指定插件的启用状态
    void set_plugin_enabled(size_t index, bool enabled);

private:
    PluginManager() = default;
    ~PluginManager();

    struct LoadedPlugin {
        HMODULE h_module = nullptr;
        IBotPlugin* instance = nullptr;
        DestroyPluginFunc destroy_func = nullptr;
        PluginInfo info;
    };

    std::vector<LoadedPlugin> loaded_plugins_;
};
