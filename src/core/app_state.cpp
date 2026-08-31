#include "core/app_state.h"

#include <fstream>
#include <sstream>

namespace mqt::core {

AppStateStore::AppStateStore(std::string path)
    : path_(std::move(path))
{
}

bool AppStateStore::load()
{
    values_.clear();
    safeMode_ = false;

    std::ifstream input(path_);
    if (!input) {
        schemaVersion_ = kCurrentSchemaVersion; // 首次启动
        return true;
    }

    // 逐行解析 key=value；损坏行（无 = 或重复 schema 版本）触发安全模式
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("schemaVersion=", 0) == 0) {
            try {
                schemaVersion_ = static_cast<std::uint32_t>(
                    std::stoul(line.substr(14)));
            } catch (...) {
                safeMode_ = true;
            }
            continue;
        }
        const auto sep = line.find('=');
        if (sep == std::string::npos || sep == 0) {
            safeMode_ = true;
            continue;
        }
        values_[line.substr(0, sep)] = line.substr(sep + 1);
    }
    if (safeMode_) {
        return false; // 损坏：调用方进入安全模式
    }
    if (schemaVersion_ < kCurrentSchemaVersion) {
        migrate(schemaVersion_);
        schemaVersion_ = kCurrentSchemaVersion;
        (void)flush();
    }
    return true;
}

void AppStateStore::migrate(std::uint32_t fromVersion)
{
    // v1 → v2：旧键 "recentPaths" 迁移到 "recent.files"
    if (fromVersion < 2) {
        const auto it = values_.find("recentPaths");
        if (it != values_.end()) {
            values_["recent.files"] = it->second;
            values_.erase("recentPaths");
        }
    }
}

bool AppStateStore::flush() const
{
    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << "schemaVersion=" << kCurrentSchemaVersion << "\n";
    for (const auto& [k, v] : values_) {
        output << k << "=" << v << "\n";
    }
    return true;
}

void AppStateStore::set(const std::string& key, const std::string& value)
{
    values_[key] = value;
}

std::optional<std::string> AppStateStore::get(const std::string& key) const
{
    const auto it = values_.find(key);
    return it != values_.end() ? std::optional{it->second} : std::nullopt;
}

void AppStateStore::remove(const std::string& key)
{
    values_.erase(key);
}

} // namespace mqt::core
