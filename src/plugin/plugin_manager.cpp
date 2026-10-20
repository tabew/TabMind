#include "plugin_manager.hpp"
#include "ui_window.hpp"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

PluginManager& PluginManager::get_instance() {
    static PluginManager instance;
    return instance;
}

PluginManager::~PluginManager() {
    unload_all();
}

void PluginManager::load_plugins(const std::string& plugin_dir) {
    unload_all();

    // 确保 plugins 目录存在
    try {
        if (!fs::exists(plugin_dir)) {
            fs::create_directories(plugin_dir);
            MainWindow::get_instance().log("📁 已自动创建插件目录: ./" + plugin_dir);
        }
    } catch (...) {}

    int count = 0;
    try {
        for (const auto& entry : fs::directory_iterator(plugin_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".dll") {
                std::string dll_path = entry.path().string();
                std::wstring wpath = entry.path().wstring();

                HMODULE hMod = LoadLibraryW(wpath.c_str());
                if (!hMod) {
                    MainWindow::get_instance().log("❌ 无法加载插件 DLL: " + dll_path);
                    continue;
                }

                CreatePluginFunc create_fn = (CreatePluginFunc)GetProcAddress(hMod, "create_plugin");
                DestroyPluginFunc destroy_fn = (DestroyPluginFunc)GetProcAddress(hMod, "destroy_plugin");

                if (!create_fn || !destroy_fn) {
                    MainWindow::get_instance().log("⚠️ 动态库缺少导出函数 create_plugin / destroy_plugin: " + dll_path);
                    FreeLibrary(hMod);
                    continue;
                }

                IBotPlugin* plugin = create_fn();
                if (!plugin) {
                    MainWindow::get_instance().log("❌ 实例化插件对象失败: " + dll_path);
                    FreeLibrary(hMod);
                    continue;
                }

                if (!plugin->on_load()) {
                    MainWindow::get_instance().log("⚠️ 插件初始化 on_load 返回失败: " + dll_path);
                    destroy_fn(plugin);
                    FreeLibrary(hMod);
                    continue;
                }

                LoadedPlugin lp;
                lp.h_module = hMod;
                lp.instance = plugin;
                lp.destroy_func = destroy_fn;
                lp.info = plugin->get_info();
                lp.info.file_path = dll_path;
                lp.info.enabled = true;

                loaded_plugins_.push_back(lp);
                count++;

                MainWindow::get_instance().log("🔌 [插件装载] " + lp.info.name + " v" + lp.info.version + " (" + lp.info.author + ") - " + lp.info.description);
            }
        }
    } catch (const std::exception& e) {
        MainWindow::get_instance().log(std::string("⚠️ 扫描插件目录异常: ") + e.what());
    }

    if (count == 0) {
        MainWindow::get_instance().log("ℹ️ 插件目录 (" + plugin_dir + ") 暂无有效 .dll 插件。");
    } else {
        MainWindow::get_instance().log("✅ 成功装载 " + std::to_string(count) + " 个 C++ 扩展插件！");
    }
}

void PluginManager::unload_all() {
    for (auto& lp : loaded_plugins_) {
        if (lp.instance) {
            try {
                lp.instance->on_unload();
            } catch (...) {}
            if (lp.destroy_func) {
                lp.destroy_func(lp.instance);
            }
        }
        if (lp.h_module) {
            FreeLibrary(lp.h_module);
        }
    }
    loaded_plugins_.clear();
}

bool PluginManager::dispatch_message(const PluginContext& ctx,
                                     const std::function<bool(const PluginInfo&)>& allow_filter) {
    for (auto& lp : loaded_plugins_) {
        if (!lp.info.enabled || !lp.instance) continue;
        if (allow_filter && !allow_filter(lp.info)) continue;
        try {
            bool handled = lp.instance->on_message(ctx);
            if (handled) {
                // 插件拦截并处理了该消息
                return true;
            }
        } catch (const std::exception& e) {
            MainWindow::get_instance().log("❌ 插件 [" + lp.info.name + "] 处理消息抛出异常: " + e.what());
        } catch (...) {
            MainWindow::get_instance().log("❌ 插件 [" + lp.info.name + "] 处理消息发生未知异常。");
        }
    }
    return false;
}

void PluginManager::dispatch_ai_reply(const PluginContext& ctx, std::string& ai_reply_text) {
    for (auto& lp : loaded_plugins_) {
        if (!lp.info.enabled || !lp.instance) continue;
        try {
            lp.instance->on_ai_reply(ctx, ai_reply_text);
        } catch (const std::exception& e) {
            MainWindow::get_instance().log("❌ 插件 [" + lp.info.name + "] 后置处理异常: " + e.what());
        } catch (...) {
            MainWindow::get_instance().log("❌ 插件 [" + lp.info.name + "] 后置处理发生未知异常。");
        }
    }
}

std::vector<PluginInfo> PluginManager::get_plugin_list() const {
    std::vector<PluginInfo> list;
    for (const auto& lp : loaded_plugins_) {
        list.push_back(lp.info);
    }
    return list;
}

void PluginManager::set_plugin_enabled(size_t index, bool enabled) {
    if (index < loaded_plugins_.size()) {
        loaded_plugins_[index].info.enabled = enabled;
    }
}
