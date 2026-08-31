// 批次 16：状态存储迁移/安全模式测试
#include "core/app_state.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testLoadMigrate()
{
    const auto path = std::filesystem::temp_directory_path() / "mqt_state1.cfg";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "schemaVersion=1\nrecentPaths=/old/path\n";
    }
    mqt::core::AppStateStore store(path.string());
    require(store.load(), "v1 state loads");
    require(store.schemaVersion() == mqt::core::AppStateStore::kCurrentSchemaVersion,
        "migrated to current schema");
    require(store.get("recent.files").value_or("") == "/old/path",
        "v1 recentPaths migrated to recent.files");
    std::filesystem::remove(path);
}

void testCorruptStateSafeMode()
{
    const auto path = std::filesystem::temp_directory_path() / "mqt_state2.cfg";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "schemaVersion=garbage\nno-separator-line\n";
    }
    mqt::core::AppStateStore store(path.string());
    require(!store.load(), "corrupt state must fail load");
    require(store.safeMode(), "safe mode must be active");
    // 安全模式不覆盖损坏文件（诊断保留）
    std::ifstream input(path);
    require(input.good(), "corrupt file preserved for diagnosis");

    // 首次启动
    const auto fresh = std::filesystem::temp_directory_path() / "mqt_state3.cfg";
    std::filesystem::remove(fresh);
    mqt::core::AppStateStore freshStore(fresh.string());
    require(freshStore.load(), "first launch loads cleanly");
    require(freshStore.schemaVersion() == mqt::core::AppStateStore::kCurrentSchemaVersion,
        "first launch at current schema");
    std::filesystem::remove(fresh);
}

void testSetGetRemove()
{
    const auto path = std::filesystem::temp_directory_path() / "mqt_state4.cfg";
    std::filesystem::remove(path);
    mqt::core::AppStateStore store(path.string());
    require(store.load(), "load");
    store.set("theme", "dark");
    require(store.get("theme").value_or("") == "dark", "set/get");
    store.remove("theme");
    require(!store.get("theme").has_value(), "remove works");
    store.flush();
    mqt::core::AppStateStore reloaded(path.string());
    require(reloaded.load(), "reloaded");
    require(!reloaded.get("theme").has_value(), "removal persisted");
    std::filesystem::remove(path);
}

} // namespace

int main()
{
    try {
        testLoadMigrate();
        testCorruptStateSafeMode();
        testSetGetRemove();
        std::cout << "app state tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
