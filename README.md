# TabMind — 零依赖 C++17 QQ AI 机器人框架

> **TabMind** = Tab + Mind · 由 tabew 打造的 QQ 智能体管理终端

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-blue.svg)]()
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)]()

基于 **C++17 + Win32 原生 GUI** 的 QQ AI 机器人框架，通过标准 **OneBot v11** 协议（NapCat / LLOneBot / Lagrange）对接 QQ。
**不依赖任何第三方库**：HTTP、WebSocket、JSON 全部是自带的轻量实现，只要有 Windows SDK 就能编译。

---

## 特性

- **零外部依赖** — JSON / HTTP / WebSocket 均为内置实现（`include/simple_*.hpp`），无需 vcpkg、无需 OpenSSL
- **原生 GUI** — Win32 编写的配置管理器，可视化改配置、启停机器人、实时日志
- **插件系统** — `plugins/` 目录下的 DLL 自动扫描加载，热插拔；插件可拦截消息免消耗 AI Token
- **群聊 @ 门禁** — 群内必须 @机器人 或命中指令前缀才响应，可配置免@白名单插件，防止刷屏
- **AI 能力** — 多轮上下文记忆、长期记忆总结、图片多模态识别、自主判断是否接话（可沉默）
- **多账号/多场景** — 私聊、群聊分别维护上下文，支持引用回复

---

## 环境要求

| 项目 | 要求 |
|---|---|
| 系统 | Windows 10 / 11 x64 |
| 编译器 | Visual Studio 2022（v143 工具集）+ Windows SDK 10 |
| 协议端 | [NapCat](https://github.com/NapNeko/NapCatQQ) / LLOneBot / Lagrange（任一，需开启正向 WebSocket） |
| 依赖 | 无（仅 Windows 系统库：`winhttp`、`ws2_32`、`gdiplus` 等） |

---

## 快速开始

### 1. 编译

**方式 A：Visual Studio（推荐）**

双击打开 `TabMind.sln` → 选择 `Release | x64` → 生成。产物在 `bin\Release\TabMind.exe`。

**方式 B：CMake**

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

**方式 C：命令行（MSBuild 被策略拦截时）**

```powershell
# 在 "x64 Native Tools Command Prompt" 里执行
cl /nologo /c /std:c++17 /utf-8 /EHsc /O2 /MT /D NOMINMAX /D _CRT_SECURE_NO_WARNINGS ^
   /D NDEBUG /D _WINDOWS /I include ^
   src\core\main.cpp src\core\bot_client.cpp src\ai\ai_client.cpp ^
   src\message\message_handler.cpp src\plugin\plugin_manager.cpp src\ui\ui_window.cpp
link /SUBSYSTEM:WINDOWS /OUT:TabMind.exe *.obj ^
   winhttp.lib ws2_32.lib crypt32.lib comctl32.lib uxtheme.lib dwmapi.lib gdiplus.lib msimg32.lib
```

### 2. 配置

复制 `config.example.json` 为 `config.json`，填入 `ai.api_key`、`onebot.bot_qq` 等字段。
也可以直接启动程序，在图形界面里填写后点「保存配置」。

### 3. 启动协议端

以管理员身份运行 NapCat 的启动脚本，用手机 QQ 扫码登录 Bot 账号，确认 WebSocket 端口与 `config.json` 中的 `onebot.ws_url` 一致（默认 `ws://127.0.0.1:3001`）。

### 4. 运行

双击 `bin\Release\TabMind.exe`（或 `TabMind.exe`），窗口内点「启动机器人」。

---

## 目录结构

```text
TabMind/
├── src/
│   ├── core/      main.cpp（入口）、bot_client.cpp（OneBot WebSocket 连接）
│   ├── ai/        ai_client.cpp（大模型调用 / 记忆 / 多模态）
│   ├── message/   message_handler.cpp（事件解析 / @门禁 / 插件派发 / AI 触发）
│   ├── plugin/    plugin_manager.cpp（DLL 扫描加载 / 派发 / AI 钩子）
│   └── ui/        ui_window.cpp（Win32 配置界面）
├── include/       公共头文件
│   ├── simple_json.hpp / simple_http.hpp / simple_ws.hpp   ← 内置基础库
│   └── ai_client.hpp / bot_client.hpp / config.hpp /
│       message_handler.hpp / plugin_manager.hpp / ui_window.hpp
├── plugins/       ⚠ 插件目录：*.dll 放这里，启动时自动加载
├── docs/          架构说明、插件开发指南
├── config.example.json
└── TabMind.sln
```

---

## 配置说明（`config.json`）

| 分组 | 字段 | 说明 |
|---|---|---|
| `ai` | `api_key` | 大模型 API Key |
| | `base_url` | OpenAI 兼容接口地址 |
| | `model` / `vision_model` | 对话模型 / 视觉模型 |
| | `system_prompt` | 人设与行为约束 |
| | `max_history_rounds` | 上下文保留轮数 |
| | `close_friends` | 亲近好友 QQ 列表（影响接话策略） |
| `onebot` | `ws_url` | 正向 WebSocket 地址 |
| | `bot_qq` | 机器人自身 QQ 号（用于 @ 判定） |
| | `trigger_prefixes` | 指令前缀，如 `/chat`、`/ai` |
| | `group_require_at` | 群聊是否必须 @ 或命中前缀才响应 |
| | `at_exempt_plugins` | 免 @ 白名单插件（按 DLL 名/插件名子串匹配） |
| | `smart_reply` | 是否允许模型自主决定沉默 |

---

## 插件开发

在 `plugins/` 下新建工程，实现 `IBotPlugin` 并导出两个 C 函数即可：

```cpp
#include "plugin_manager.hpp"

class MyPlugin : public IBotPlugin {
public:
    PluginInfo get_info() override {
        PluginInfo info;
        info.name = "示例插件";
        info.version = "1.0.0";
        info.author = "you";
        info.description = "收到「ping」时回复 pong";
        return info;
    }
    bool on_load() override { return true; }
    void on_unload() override {}
    bool on_message(const PluginContext& ctx) override {
        if (ctx.plain_text == "ping") {
            ctx.send_reply(ctx.message_type,
                           ctx.message_type == "group" ? ctx.group_id : ctx.user_id,
                           "pong");
            return true;   // true = 已处理，不再交给 AI（省 Token）
        }
        return false;      // false = 交给 AI 继续处理
    }
};

extern "C" __declspec(dllexport) IBotPlugin* create_plugin() { return new MyPlugin(); }
extern "C" __declspec(dllexport) void destroy_plugin(IBotPlugin* p) { delete p; }
```

编译成 DLL 后放进 `plugins/`，重启即生效。后台线程回发消息请用线程安全的 `ctx.async_send_reply`，不要跨线程持有 `std::function`。

详见 **[docs/插件开发指南.md](docs/插件开发指南.md)**。

---

## 注意事项

- `config.json` 含 API Key，**不要提交到版本库**（已在 `.gitignore` 中排除）
- 机器人的能力边界取决于所接的大模型；请遵守所在地法律法规与平台规则
- 本框架仅做协议对接与消息编排，不包含任何违规内容生成逻辑

---

## License

本项目采用 [MIT License](LICENSE) 开源。

```
MIT License

Copyright (c) 2026 tabew

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction...
```

可以自由使用、修改、分发、商用，唯一要求是保留版权声明与许可声明。
