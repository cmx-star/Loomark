#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace mqt::core {

/// M69/M70 应用状态存储：带 schema 版本迁移与损坏检测（安全模式）。
/// 简单 key=value 文本格式（与元数据存储同族；SQLite 迁移在 D04 后）。
class AppStateStore {
public:
    static constexpr std::uint32_t kCurrentSchemaVersion = 2;

    explicit AppStateStore(std::string path);

    /// 加载并迁移。返回 false = 状态损坏（调用方应进入安全模式：
    /// 忽略旧状态、重建默认配置，不覆盖损坏文件以便诊断）。
    bool load();

    bool flush() const;

    void set(const std::string& key, const std::string& value);
    [[nodiscard]] std::optional<std::string> get(const std::string& key) const;
    void remove(const std::string& key);

    [[nodiscard]] std::uint32_t schemaVersion() const { return schemaVersion_; }
    [[nodiscard]] bool safeMode() const { return safeMode_; }

private:
    void migrate(std::uint32_t fromVersion);

    std::string path_;
    std::map<std::string, std::string> values_;
    std::uint32_t schemaVersion_ = 0;
    bool safeMode_ = false;
};

} // namespace mqt::core
