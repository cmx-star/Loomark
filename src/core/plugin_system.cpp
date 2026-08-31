#include "core/plugin_system.h"

#include <algorithm>
#include <sstream>

namespace mqt::core {

std::optional<PluginManifest> PluginManifest::parse(const std::string& manifestText)
{
    PluginManifest manifest;
    std::istringstream stream(manifestText);
    std::string line;
    bool hasId = false;
    while (std::getline(stream, line)) {
        const auto sep = line.find('=');
        if (sep == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, sep);
        const std::string value = line.substr(sep + 1);
        if (key == "id" && !value.empty()) {
            manifest.id = value;
            hasId = true;
        } else if (key == "version") {
            manifest.version = static_cast<std::uint32_t>(std::stoul(value));
        } else if (key == "apiVersion") {
            manifest.apiVersion = static_cast<std::uint32_t>(std::stoul(value));
        } else if (key == "platform") {
            manifest.platform = value;
        } else if (key == "permissions") {
            std::size_t start = 0;
            while (start <= value.size()) {
                const auto comma = value.find(',', start);
                if (comma == std::string::npos) {
                    if (start < value.size()) {
                        manifest.permissions.push_back(value.substr(start));
                    }
                    break;
                }
                manifest.permissions.push_back(value.substr(start, comma - start));
                start = comma + 1;
            }
        } else if (key == "signature") {
            manifest.signature = value;
        }
    }
    if (!hasId) {
        return std::nullopt;
    }
    return manifest;
}

bool PluginInstance::hasPermission(const std::string& permission) const
{
    return std::find(manifest.permissions.begin(), manifest.permissions.end(),
               permission) != manifest.permissions.end();
}

PluginHost::PluginHost(AppStateStore& state)
    : state_(state)
{
}

std::string PluginHost::load(const PluginManifest& manifest)
{
    if (manifest.id.empty()) {
        return "manifest missing plugin id";
    }
    if (manifest.apiVersion != kHostApiVersion) {
        return "incompatible api version: plugin needs " +
            std::to_string(manifest.apiVersion) + ", host provides " +
            std::to_string(kHostApiVersion);
    }
    if (!manifest.platform.empty() && manifest.platform != "any") {
        return "unsupported platform: " + manifest.platform;
    }
    // 启动禁用清单
    if (state_.get("plugin.disabled." + manifest.id).has_value()) {
        return "plugin is disabled at startup";
    }
    if (plugins_.contains(manifest.id)) {
        return "plugin already loaded";
    }
    PluginInstance instance;
    instance.manifest = manifest;
    plugins_[manifest.id] = std::move(instance);
    return {};
}

bool PluginHost::enable(const std::string& pluginId)
{
    const auto it = plugins_.find(pluginId);
    if (it == plugins_.end()) {
        return false;
    }
    it->second.enabled = true;
    state_.remove("plugin.disabled." + pluginId);
    return true;
}

bool PluginHost::disable(const std::string& pluginId)
{
    const auto it = plugins_.find(pluginId);
    if (it == plugins_.end()) {
        return false;
    }
    it->second.enabled = false;
    state_.set("plugin.disabled." + pluginId, "1");
    return true;
}

PluginInstance* PluginHost::plugin(const std::string& pluginId)
{
    const auto it = plugins_.find(pluginId);
    return it != plugins_.end() ? &it->second : nullptr;
}

std::string PluginHost::runCommand(const std::string& pluginId,
    const std::string& command)
{
    auto* instance = plugin(pluginId);
    if (instance == nullptr) {
        return "plugin not loaded";
    }
    if (!instance->enabled) {
        return "plugin disabled";
    }
    const auto it = instance->commands.find(command);
    if (it == instance->commands.end()) {
        return "unknown command";
    }
    diagnostics_.push_back("run:" + pluginId + ":" + command);
    try {
        return it->second();
    } catch (const std::exception& e) {
        diagnostics_.push_back("fault:" + pluginId + ":" + e.what());
        return "plugin fault contained: " + std::string(e.what());
    } catch (...) {
        diagnostics_.push_back("fault:" + pluginId + ":unknown");
        return "plugin fault contained";
    }
}

void PluginHost::disableAtStartup(const std::string& pluginId)
{
    state_.set("plugin.disabled." + pluginId, "1");
    disable(pluginId);
}

} // namespace mqt::core
