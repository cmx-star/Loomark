#include "core/git_service.h"

#include <cstdio>
#include <cstdlib>
#include <array>

namespace mqt::core {

namespace {
#ifdef _WIN32
constexpr const char* kGitBinary = "git.exe";
constexpr const char* kPopen = "_popen";
constexpr const char* kPclose = "_pclose";
#else
constexpr const char* kGitBinary = "git";
constexpr const char* kPopen = "popen";
constexpr const char* kPclose = "pclose";
#endif
} // namespace

GitService::GitService(std::filesystem::path workDir)
    : workDir_(std::move(workDir))
{
}

GitService::Result GitService::run(const std::vector<std::string>& args,
    bool allowFailure) const
{
    std::string command = std::string("cd '") + workDir_.string() + "' && " + kGitBinary;
    for (const auto& arg : args) {
        command += " '";
        command += arg;
        command += "'";
    }
    command += " 2>&1";

    Result result;
    std::array<char, 256> buffer{};
    FILE* pipe = ::_popen(command.c_str(), "r");
    if (pipe == nullptr) {
        result.output = "failed to spawn git";
        return result;
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }
    const int status = ::_pclose(pipe);
    result.ok = allowFailure || status == 0;
    return result;
}

bool GitService::isRepository() const
{
    return run({"rev-parse", "--is-inside-work-tree"}, true).output.find("true") !=
        std::string::npos;
}

GitService::Result GitService::currentBranch() const
{
    return run({"rev-parse", "--abbrev-ref", "HEAD"});
}

GitService::Result GitService::statusPorcelain() const
{
    return run({"status", "--porcelain"});
}

GitService::Result GitService::recentLog(int maxCount) const
{
    return run({"log", "--oneline", "-n", std::to_string(maxCount)});
}

GitService::Result GitService::stage(const std::string& pathSpec) const
{
    return run({"add", "--", pathSpec});
}

GitService::Result GitService::unstage(const std::string& pathSpec) const
{
    return run({"reset", "HEAD", "--", pathSpec});
}

GitService::Result GitService::commit(const std::string& message) const
{
    return run({"commit", "-m", message});
}

GitService::Result GitService::createBranch(const std::string& name) const
{
    return run({"branch", name});
}

GitService::Result GitService::fetch() const
{
    return run({"fetch", "--all"});
}

GitService::Result GitService::pull() const
{
    return run({"pull", "--ff-only"});
}

GitService::Result GitService::push() const
{
    return run({"push"});
}

GitService::Result GitService::discardFileChanges(const std::string& pathSpec,
    bool confirm) const
{
    if (!confirm) {
        return Result{false, "high-risk operation requires confirmation"};
    }
    return run({"checkout", "--", pathSpec});
}

GitService::Result GitService::resetHard(bool confirm) const
{
    if (!confirm) {
        return Result{false, "high-risk operation requires confirmation"};
    }
    return run({"reset", "--hard"});
}

GitService::Result GitService::forcePush(bool confirm) const
{
    if (!confirm) {
        return Result{false, "high-risk operation requires confirmation"};
    }
    return run({"push", "--force"});
}

} // namespace mqt::core
