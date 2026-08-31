#pragma once

#include "core/app_state.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mqt::core {

/// M73 插件清单：id/version/apiVersion/platform/权限（key=value 文本格式）。
struct PluginManifest {
    std::string id;
    std::uint32_t version = 0;      // 插件自身版本
    std::uint32_t apiVersion = 1;   // 需要的宿主 API 版本
    std::string platform;           // "any" / "mac" / "win" / "linux"
    std::vector<std::string> permissions; // files, workspace, network, ai, export, write
    std::string signature;          // 签名元数据（占位）

    /// 解析失败返回 nullopt。
    [[nodiscard]] static std::optional<PluginManifest> parse(
        const std::string& manifestText);
};

/// 宿主 API 版本：插件 apiVersion 必须等于宿主版本才能加载。
inline constexpr std::uint32_t kHostApiVersion = 1;

/// M74/M75 插件实例：权限 + 扩展点（命令/导出器）。
class PluginInstance {
public:
    PluginManifest manifest;
    bool enabled = false;
    /// 扩展点：注册命令（只读动作）。执行经过故障隔离。
    std::map<std::string, std::function<std::string()>> commands;
    /// M74：权限检查——动作需要声明的权限。
    [[nodiscard]] bool hasPermission(const std::string& permission) const;
};

/// M73–M76 插件宿主：加载校验、启停、故障隔离与诊断。
class PluginHost {
public:
    explicit PluginHost(AppStateStore& state);

    /// M73：加载校验（apiVersion/platform/清单完整性）。不兼容 → 返回错误。
    [[nodiscard]] std::string load(const PluginManifest& manifest);
    bool enable(const std::string& pluginId);
    bool disable(const std::string& pluginId);
    [[nodiscard]] PluginInstance* plugin(const std::string& pluginId);
    [[nodiscard]] std::size_t pluginCount() const { return plugins_.size(); }

    /// M75/M76：执行插件命令——异常被捕获（故障隔离），诊断写入日志；
    /// 需要权限的命令先查权限。返回命令输出或错误说明。
    [[nodiscard]] std::string runCommand(const std::string& pluginId,
        const std::string& command);
    [[nodiscard]] const std::vector<std::string>& diagnostics() const { return diagnostics_; }

    /// M76：启动禁用列表持久化（安全模式管理）。
    void disableAtStartup(const std::string& pluginId);

private:
    AppStateStore& state_;
    std::map<std::string, PluginInstance> plugins_;
    std::vector<std::string> diagnostics_;
};

} // namespace mqt::core
