// 批次 10：Git 集成测试（临时仓库）
#include "core/git_service.h"

#include <cstdlib>
#include <filesystem>
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

void run(const std::string& cmd)
{
    if (std::system(cmd.c_str()) != 0) {
        throw std::runtime_error(("setup command failed: " + cmd).c_str());
    }
}

void testGitFlow()
{
    const auto root = std::filesystem::temp_directory_path() / "mqt_git_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto rootStr = root.string();

    run("cd '" + rootStr + "' && git init -q");
    run("cd '" + rootStr + "' && git config user.email t@t && git config user.name t");

    const auto file = root / "doc.md";
    {
        std::ofstream output(file, std::ios::binary | std::ios::trunc);
        output << "v1\n";
    }
    run("cd '" + rootStr + "' && git add doc.md && git commit -qm init");

    mqt::core::GitService git(root);
    require(git.isRepository(), "directory recognized as repository");
    auto branch = git.currentBranch();
    require(branch.ok && (branch.output.find("master") != std::string::npos ||
                          branch.output.find("main") != std::string::npos),
        "branch detected");

    // M39：状态显示修改
    {
        std::ofstream output(file, std::ios::binary | std::ios::trunc);
        output << "v2\n";
    }
    auto status = git.statusPorcelain();
    require(status.ok && status.output.find("M doc.md") != std::string::npos,
        "modified file detected");

    // M40：暂存 → 提交
    require(git.stage("doc.md").ok, "stage succeeds");
    status = git.statusPorcelain();
    require(status.output.find("M  doc.md") != std::string::npos,
        "staged flag uses the index column");
    auto commit = git.commit("update doc");
    require(commit.ok, "commit succeeds");
    status = git.statusPorcelain();
    require(status.output.find("doc.md") == std::string::npos,
        "clean tree after commit");
    auto log = git.recentLog(2);
    require(log.ok && log.output.find("update doc") != std::string::npos,
        "recent log shows the commit");

    // M40：建分支
    require(git.createBranch("feature-x").ok, "branch created");
    branch = git.currentBranch();
    require(branch.output.find("feature-x") == std::string::npos,
        "branch creation does not switch");

    // M42：高风险操作需要确认
    {
        std::ofstream output(file, std::ios::binary | std::ios::trunc);
        output << "uncommitted\n";
    }
    auto denied = git.discardFileChanges("doc.md", false);
    require(!denied.ok &&
            denied.output.find("confirmation") != std::string::npos,
        "discard without confirm is refused");
    auto discarded = git.discardFileChanges("doc.md", true);
    require(discarded.ok, "discard with confirm executes");
    require(git.statusPorcelain().output.find("doc.md") == std::string::npos,
        "discard reverted the file");

    std::filesystem::remove_all(root);
}

} // namespace

int main()
{
    try {
        // git 可用性前置检查
        if (std::system("git --version >/dev/null 2>&1") != 0) {
            std::cout << "git not available, skipping\n";
            return EXIT_SUCCESS;
        }
        testGitFlow();
        std::cout << "git service tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
