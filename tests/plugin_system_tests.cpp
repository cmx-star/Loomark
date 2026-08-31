// 批次 17：插件清单/兼容性/权限/隔离 测试
#include "core/plugin_system.h"

#include <filesystem>
#include <iostream>
#include <algorithm>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testManifestAndCompatibility()
{
    const auto manifest = mqt::core::PluginManifest::parse(
        "id=sample.plugin\nversion=3\napiVersion=1\nplatform=any\n"
        "permissions=workspace,export\n");
    require(manifest.has_value(), "valid manifest parses");
    require(manifest->id == "sample.plugin", "id parsed");
    require(std::find(manifest->permissions.begin(), manifest->permissions.end(),
                "workspace") != manifest->permissions.end() &&
            std::find(manifest->permissions.begin(), manifest->permissions.end(),
                "export") != manifest->permissions.end(), "permissions parsed");

    require(!mqt::core::PluginManifest::parse("version=1").has_value(),
        "manifest without id rejected");

    const auto path = std::filesystem::temp_directory_path() / "mqt_plugins.cfg";
    std::filesystem::remove(path);
    mqt::core::AppStateStore state(path.string());
    (void)state.load();
    mqt::core::PluginHost host(state);

    // api 版本不兼容
    auto incompatible = *manifest;
    incompatible.apiVersion = 99;
    require(!host.load(incompatible).empty(), "incompatible api rejected");

    // 平台不兼容
    auto wrongPlatform = *manifest;
    wrongPlatform.platform = "win";
    require(!host.load(wrongPlatform).empty(), "wrong platform rejected");

    // 正常加载
    require(host.load(*manifest).empty(), "compatible plugin loads");
    require(!host.load(*manifest).empty(), "duplicate load rejected");
}

void testPermissionsAndIsolation()
{
    const auto path = std::filesystem::temp_directory_path() / "mqt_plugins2.cfg";
    std::filesystem::remove(path);
    mqt::core::AppStateStore state(path.string());
    (void)state.load();
    mqt::core::PluginHost host(state);

    auto manifest = mqt::core::PluginManifest::parse(
        "id=ok.plugin\napiVersion=1\nplatform=any\npermissions=workspace\n");
    require(host.load(*manifest).empty(), "loads");
    auto* instance = host.plugin("ok.plugin");
    require(instance != nullptr && !instance->enabled, "loaded but disabled by default");

    require(host.runCommand("ok.plugin", "anything") == "plugin disabled",
        "disabled plugin commands refused");
    require(host.enable("ok.plugin"), "enable");
    require(!host.enable("missing"), "enable unknown fails");

    // 故障隔离：抛异常的命令不传播
    instance->commands["faulty"] = []() -> std::string {
        throw std::runtime_error("boom");
    };
    const auto out = host.runCommand("ok.plugin", "faulty");
    require(out.find("plugin fault contained") != std::string::npos,
        "fault contained");
    require(!host.diagnostics().empty(), "diagnostics recorded");

    // 正常命令
    instance->commands["hello"] = []() -> std::string { return "hi from plugin"; };
    require(host.runCommand("ok.plugin", "hello") == "hi from plugin",
        "healthy command works");

    // 启动禁用持久化
    host.disableAtStartup("ok.plugin");
    require(!host.plugin("ok.plugin")->enabled, "startup-disable applied");

    // 禁用的插件不阻碍宿主核心（宿主可继续加载其它插件）
    auto other = mqt::core::PluginManifest::parse(
        "id=other.plugin\napiVersion=1\nplatform=any\n");
    require(host.load(*other).empty(), "host keeps working with a disabled plugin");
    std::filesystem::remove(path);
}

} // namespace

int main()
{
    try {
        testManifestAndCompatibility();
        testPermissionsAndIsolation();
        std::cout << "plugin system tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
