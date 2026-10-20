#pragma once
#include <windows.h>
#include <commctrl.h>
#include "config.hpp"

// ---------------------------------------------------------------------------
// 二级弹窗集合：把不常用但重要的配置收进「点开才显示」的小窗口，
// 主界面只保留高频项，避免一屏塞满控件。
// 全部为代码创建的模态窗口，不依赖 .rc 资源文件。
// ---------------------------------------------------------------------------
namespace ui_dialogs {

// 高级设置：温度 / Max Tokens / 上下文 / 冷却 / 记忆参数 / 视觉模型 /
//           认证方式 / 自动删图 / 思考模式 / 宽松接话 / 亲近好友
// 返回 true 表示用户点了「保存」（cfg 已被就地修改）
bool show_advanced_settings(HWND parent, AppConfig& cfg, HINSTANCE inst);

// 插件管理：列出已加载插件，支持启用/停用、重载、打开目录
void show_plugin_manager(HWND parent, HINSTANCE inst);

// 使用说明：指令速查 + 配置说明 + 注意事项
void show_help(HWND parent, HINSTANCE inst);

} // namespace ui_dialogs
