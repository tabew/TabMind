#include "plugin_manager.hpp"
#include "platform.hpp"
#include <filesystem>
#include <iostream>
#include <string>

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// 平台差异集中在这里：
//   Windows → LoadLibraryW / GetProcAddress / FreeLibrary，插件后缀 .dll
//   Linux   → dlopen / dlsym / dlclose，插件后缀 .so
// 插件 ABI 一致（create_plugin / destroy_plugin），但二进制不可跨平台混用：
// Windows 的 .dll 不能拿到 Linux 上加载，必须在 Linux 上重新编译成 .so。
// ---------------------------------------------------------------------------
namespace {

constexpr const char* kPluginExt =
#ifdef _WIN32
    ".dll";
#else
    ".so";
#endif

PluginModuleHandle module_open(const fs::path& path) {
#ifdef _WIN32
    return LoadLibraryW(path.wstring().c_str());
#else
    // RTLD_NOW：立即解析符号，插件缺符号时尽早暴露问题
    // RTLD_LOCAL：插件之间的符号不互相污染
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* module_symbol(PluginModuleHandle mod, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(mod, name));
#else
    return dlsym(mod, name);
#endif
}

void module_close(PluginModuleHandle mod) {
    if (!mod) return;
#ifdef _WIN32
    FreeLibrary(mod);
#else
    dlclose(mod);
#endif
}

std::string module_error() {
#ifdef _WIN32
    return "LoadLibrary failed (code " + std::to_string(GetLastError()) + ")";
#else
    const char* e = dlerror();
    return e ? e : "dlopen failed";
#endif
}

}  // namespace

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
            platform::ensure_dir(plugin_dir);
            platform::log("📁 已自动创建插件目录: ./" + plugin_dir);
        }
    } catch (...) {}

    int count = 0;
    try {
        for (const auto& entry : fs::directory_iterator(plugin_dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != kPluginExt) {
                continue;
            }

            std::string lib_path = entry.path().string();
            PluginModuleHandle hMod = module_open(entry.path());
            if (!hMod) {
                platform::log("❌ 无法加载插件 " + std::string(kPluginExt) +
                              ": " + lib_path + " (" + module_error() + ")");
                continue;
            }

            CreatePluginFunc create_fn =
                reinterpret_cast<CreatePluginFunc>(module_symbol(hMod, "create_plugin"));
            DestroyPluginFunc destroy_fn =
                reinterpret_cast<DestroyPluginFunc>(module_symbol(hMod, "destroy_plugin"));

            if (!create_fn || !destroy_fn) {
                platform::log("⚠️ 动态库缺少导出函数 create_plugin / destroy_plugin: " + lib_path);
                module_close(hMod);
                continue;
            }

            IBotPlugin* plugin = create_fn();
            if (!plugin) {
                platform::log("❌ 实例化插件对象失败: " + lib_path);
                module_close(hMod);
                continue;
            }

            if (!plugin->on_load()) {
                platform::log("⚠️ 插件初始化 on_load 返回失败: " + lib_path);
                destroy_fn(plugin);
                module_close(hMod);
                continue;
            }

            LoadedPlugin lp;
            lp.h_module = hMod;
            lp.instance = plugin;
            lp.destroy_func = destroy_fn;
            lp.info = plugin->get_info();
            lp.info.file_path = lib_path;
            lp.info.enabled = true;

            loaded_plugins_.push_back(lp);
            count++;

            platform::log("🔌 [插件装载] " + lp.info.name + " v" + lp.info.version +
                          " (" + lp.info.author + ") - " + lp.info.description);
        }
    } catch (const std::exception& e) {
        platform::log(std::string("⚠️ 扫描插件目录异常: ") + e.what());
    }

    if (count == 0) {
        platform::log("ℹ️ 插件目录 (" + plugin_dir + ") 暂无有效 " +
                      std::string(kPluginExt) + " 插件。");
    } else {
        platform::log("✅ 成功装载 " + std::to_string(count) + " 个 C++ 扩展插件！");
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
            module_close(lp.h_module);
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
            platform::log("❌ 插件 [" + lp.info.name + "] 处理消息抛出异常: " + e.what());
        } catch (...) {
            platform::log("❌ 插件 [" + lp.info.name + "] 处理消息发生未知异常。");
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
            platform::log("❌ 插件 [" + lp.info.name + "] 后置处理异常: " + e.what());
        } catch (...) {
            platform::log("❌ 插件 [" + lp.info.name + "] 后置处理发生未知异常。");
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
