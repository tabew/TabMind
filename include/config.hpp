#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include "simple_json.hpp"

struct OneBotConfig {
    std::string ws_url = "ws://127.0.0.1:3001";
    std::string access_token = "";
    int64_t bot_qq = 0;
    // 群聊必须 @机器人（或命中触发前缀）才响应：AI 与插件均生效，防止刷屏
    bool group_require_at = true;
    // 免@插件名单：群聊未@时仍可响应的插件（按 DLL 文件名或插件显示名子串匹配，忽略大小写）
    // 例如 qqy 插件(waifu_manager) 与 随机图片插件(gallery_manager)
    std::vector<std::string> at_exempt_plugins = {"waifu_manager", "gallery_manager"};
    // 自主判断是否回复：false 时仅 @/前缀触发；true 时未@也交给模型判断该不该说话
    bool smart_reply = true;
    std::vector<std::string> trigger_prefixes = {"/chat", "/ai", "你好"};
};

struct AIConfig {
    std::string base_url = "https://api.xiaomimimo.com/v1";
    std::string api_key = "";
    std::string model = "mimo-v2.6-pro";
    // 视觉模型，留空则与 model 相同
    std::string vision_model = "";
    // bearer | api-key | both（默认 both，兼容 DeepSeek 与小米 MiMo）
    // 鉴权头风格：auto（按 base_url 自动判断，推荐）/ bearer / api-key / both
    // auto 最省心：DeepSeek 官方用 Bearer，小米 MiMo 用 api-key，
    // 填错会直接 401，所以默认走自动判断。
    std::string auth_style = "auto";
    // 关闭思考模式：防止 reasoning 吃光 max_tokens 导致 content 为空（识图失败常见原因）
    bool enable_thinking = false;
    std::string system_prompt = "你是一个真实自然、富有幽默感和同理心的 QQ 好友。请用口语化、接地气的短句与我交流，每次回复尽量只说 1~2 句话，不要长篇大论，避免分点罗列或书面说教。你能看到群友发的图片，请结合图片内容回应。";
    float temperature = 0.7f;
    int max_tokens = 1000;
    int max_history_rounds = 10;
    // 长期记忆简写上限（字，按 Unicode 字符计）
    int memory_max_chars = 1000;
    // 攒够多少条新消息后触发一次自动简写
    int memory_batch_size = 8;
    // 看完/记完本地图片后自动删除，节省磁盘
    bool auto_delete_images = true;
    // 关系很近的 QQ：这些人说话可以主动接（不必@）
    std::vector<std::string> close_friends;
    // 同一群两次主动插话的最短间隔（秒），防止话痨
    int reply_cooldown_sec = 45;
};

// ---- 界面布局：模块可自由拖动/缩放，位置持久化到 config.json ----
struct UILayoutItem {
    int x = 0, y = 0, w = 0, h = 0;
    bool visible = true;
};

struct UIConfig {
    int theme = 0;                       // 主题索引（见 ui_theme.hpp）
    bool locked = false;                 // 锁定布局：禁止拖动/缩放
    std::vector<UILayoutItem> modules;   // 为空表示使用默认布局
    // 布局版本号。UI 大改（面板数量/默认排布变化）时递增，
    // 这样老配置文件里残留的旧布局会被自动丢弃、改用新默认排布，
    // 不会让用户升级后面对一堆错位/隐藏的面板。
    //   0（或缺失）= v3 及更早的子窗口卡片布局
    //   4          = v4 单窗口全自绘三分栏
    int layout_ver = 0;
};

// 当前 UI 布局版本（改 UI 排布时同步递增）
inline constexpr int kUiLayoutVersion = 4;

struct AppConfig {
    OneBotConfig onebot;
    AIConfig ai;
    UIConfig ui;

    static AppConfig load_from_file(const std::string& path = "config.json") {
        AppConfig config;
        std::ifstream file(path);
        if (!file.is_open()) {
            config.save_to_file(path);
            return config;
        }

        try {
            nlohmann::json j;
            file >> j;

            if (j.contains("onebot")) {
                auto ob = j["onebot"];
                if (ob.contains("ws_url")) config.onebot.ws_url = ob["ws_url"].get<std::string>();
                if (ob.contains("access_token")) config.onebot.access_token = ob["access_token"].get<std::string>();
                if (ob.contains("bot_qq")) config.onebot.bot_qq = ob["bot_qq"].get<int64_t>();
                if (ob.contains("group_require_at")) config.onebot.group_require_at = ob["group_require_at"].get<bool>();
                if (ob.contains("at_exempt_plugins")) config.onebot.at_exempt_plugins = ob["at_exempt_plugins"].get<std::vector<std::string>>();
                if (ob.contains("smart_reply")) config.onebot.smart_reply = ob["smart_reply"].get<bool>();
                if (ob.contains("trigger_prefixes")) config.onebot.trigger_prefixes = ob["trigger_prefixes"].get<std::vector<std::string>>();
            }

            if (j.contains("ai")) {
                auto ai = j["ai"];
                if (ai.contains("base_url")) config.ai.base_url = ai["base_url"].get<std::string>();
                if (ai.contains("api_key")) config.ai.api_key = ai["api_key"].get<std::string>();
                if (ai.contains("model")) config.ai.model = ai["model"].get<std::string>();
                if (ai.contains("vision_model")) config.ai.vision_model = ai["vision_model"].get<std::string>();
                if (ai.contains("auth_style")) config.ai.auth_style = ai["auth_style"].get<std::string>();
                if (ai.contains("enable_thinking")) config.ai.enable_thinking = ai["enable_thinking"].get<bool>();
                if (ai.contains("system_prompt")) config.ai.system_prompt = ai["system_prompt"].get<std::string>();
                if (ai.contains("temperature")) config.ai.temperature = ai["temperature"].get<float>();
                if (ai.contains("max_tokens")) config.ai.max_tokens = ai["max_tokens"].get<int>();
                if (ai.contains("max_history_rounds")) config.ai.max_history_rounds = ai["max_history_rounds"].get<int>();
                if (ai.contains("memory_max_chars")) config.ai.memory_max_chars = ai["memory_max_chars"].get<int>();
                if (ai.contains("memory_batch_size")) config.ai.memory_batch_size = ai["memory_batch_size"].get<int>();
                if (ai.contains("auto_delete_images")) config.ai.auto_delete_images = ai["auto_delete_images"].get<bool>();
                if (ai.contains("close_friends")) config.ai.close_friends = ai["close_friends"].get<std::vector<std::string>>();
                if (ai.contains("reply_cooldown_sec")) config.ai.reply_cooldown_sec = ai["reply_cooldown_sec"].get<int>();
            }

            if (j.contains("ui")) {
                auto uj = j["ui"];
                if (uj.contains("theme")) config.ui.theme = uj["theme"].get<int>();
                if (uj.contains("locked")) config.ui.locked = uj["locked"].get<bool>();
                if (uj.contains("layout_ver")) config.ui.layout_ver = uj["layout_ver"].get<int>();
                if (uj.contains("modules") && uj["modules"].is_array()) {
                    config.ui.modules.clear();
                    for (size_t i = 0; i < uj["modules"].size(); ++i) {
                        auto m = uj["modules"][i];
                        UILayoutItem it;
                        if (m.contains("x")) it.x = m["x"].get<int>();
                        if (m.contains("y")) it.y = m["y"].get<int>();
                        if (m.contains("w")) it.w = m["w"].get<int>();
                        if (m.contains("h")) it.h = m["h"].get<int>();
                        if (m.contains("visible")) it.visible = m["visible"].get<bool>();
                        config.ui.modules.push_back(it);
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[Config] 解析配置文件异常: " << e.what() << std::endl;
        }

        return config;
    }

    void save_to_file(const std::string& path = "config.json") const {
        nlohmann::json j = nlohmann::json::object();
        
        nlohmann::json ob = nlohmann::json::object();
        ob["ws_url"] = onebot.ws_url;
        ob["access_token"] = onebot.access_token;
        ob["bot_qq"] = onebot.bot_qq;
        ob["group_require_at"] = onebot.group_require_at;
        {
            nlohmann::json exempt = nlohmann::json::array();
            for (const auto& p : onebot.at_exempt_plugins) {
                exempt.push_back(p);
            }
            ob["at_exempt_plugins"] = exempt;
        }
        ob["smart_reply"] = onebot.smart_reply;

        nlohmann::json prefixes = nlohmann::json::array();
        for (const auto& p : onebot.trigger_prefixes) {
            prefixes.push_back(p);
        }
        ob["trigger_prefixes"] = prefixes;
        j["onebot"] = ob;

        nlohmann::json ai_obj = nlohmann::json::object();
        ai_obj["base_url"] = ai.base_url;
        ai_obj["api_key"] = ai.api_key;
        ai_obj["model"] = ai.model;
        ai_obj["vision_model"] = ai.vision_model;
        ai_obj["auth_style"] = ai.auth_style;
        ai_obj["enable_thinking"] = ai.enable_thinking;
        ai_obj["system_prompt"] = ai.system_prompt;
        ai_obj["temperature"] = ai.temperature;
        ai_obj["max_tokens"] = ai.max_tokens;
        ai_obj["max_history_rounds"] = ai.max_history_rounds;
        ai_obj["memory_max_chars"] = ai.memory_max_chars;
        ai_obj["memory_batch_size"] = ai.memory_batch_size;
        ai_obj["auto_delete_images"] = ai.auto_delete_images;
        {
            nlohmann::json friends = nlohmann::json::array();
            for (const auto& f : ai.close_friends) {
                friends.push_back(f);
            }
            ai_obj["close_friends"] = friends;
        }
        ai_obj["reply_cooldown_sec"] = ai.reply_cooldown_sec;
        j["ai"] = ai_obj;

        nlohmann::json ui_obj = nlohmann::json::object();
        ui_obj["theme"] = ui.theme;
        ui_obj["locked"] = ui.locked;
        ui_obj["layout_ver"] = kUiLayoutVersion;
        {
            nlohmann::json mods = nlohmann::json::array();
            for (const auto& m : ui.modules) {
                nlohmann::json mo = nlohmann::json::object();
                mo["x"] = m.x;  mo["y"] = m.y;
                mo["w"] = m.w;  mo["h"] = m.h;
                mo["visible"] = m.visible;
                mods.push_back(mo);
            }
            ui_obj["modules"] = mods;
        }
        j["ui"] = ui_obj;

        std::ofstream file(path);
        if (file.is_open()) {
            file << j.dump(4);
        }
    }
};
