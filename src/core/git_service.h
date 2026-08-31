#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mqt::core {

/// M39–M43 Git 集成（基于 git CLI）。
/// 写操作必须显式触发；高风险操作（丢弃/回退/强推）需要 confirm=true，
/// 否则返回需要确认的错误且不执行。
class GitService {
public:
    struct Result {
        bool ok = false;
        std::string output;   // stdout（成功）或 stderr（失败）
    };

    explicit GitService(std::filesystem::path workDir);

    /// M39：目录是否为 git 仓库（含父目录）。
    [[nodiscard]] bool isRepository() const;
    /// M39：当前分支名（ detached → "HEAD"）。
    [[nodiscard]] Result currentBranch() const;
    /// M39：porcelain v1 状态行（"XY path"）。
    [[nodiscard]] Result statusPorcelain() const;
    /// M39：最近 N 条提交（"hash subject"）。
    [[nodiscard]] Result recentLog(int maxCount) const;

    /// M40：暂存 / 取消暂存 / 提交 / 建分支。仅显式触发。
    Result stage(const std::string& pathSpec) const;
    Result unstage(const std::string& pathSpec) const;
    Result commit(const std::string& message) const;
    Result createBranch(const std::string& name) const;

    /// M41：网络操作——仅执行并返回结果，从不自动触发。
    Result fetch() const;
    Result pull() const;
    Result push() const;

    /// M42：高风险操作——discardFile / resetHard / forcePush 必须 confirm=true。
    Result discardFileChanges(const std::string& pathSpec, bool confirm) const;
    Result resetHard(bool confirm) const;
    Result forcePush(bool confirm) const;

private:
    Result run(const std::vector<std::string>& args, bool allowFailure = false) const;
    std::filesystem::path workDir_;
};

} // namespace mqt::core
