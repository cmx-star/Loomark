// 批次 14：EditProposal 校验/应用/影响 Diff 测试
#include "ai/edit_proposal.h"
#include "core/document_backend.h"

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

void testProposalLifecycle()
{
    const auto path = std::filesystem::temp_directory_path() / "proposal.md";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "line1\nline2\nline3\n";
    }
    mqt::core::FileDocumentBackend backend(path);

    mqt::ai::EditProposal proposal;
    proposal.baseVersion = backend.snapshot().version;
    proposal.baseFingerprint = backend.fingerprint();
    proposal.edits.push_back({6, 11, "LINE TWO"});
    proposal.explanation = "强调第二行";
    proposal.source = "ai:test";

    // 校验通过；未应用前文档不变（默认不写入）
    auto check = mqt::ai::EditProposalEvaluator::validate(proposal, backend);
    require(check == mqt::ai::ProposalCheck::Ok, "valid proposal");
    require(backend.read({mqt::core::ByteRange{0, 17}}) == "line1\nline2\nline3",
        "proposal must not touch the document");

    // 应用 → 内容变化、版本递增、可撤销（单撤销组）
    std::uint64_t newVersion = 0;
    require(mqt::ai::EditProposalEvaluator::applyProposal(
                proposal, backend, &newVersion, nullptr),
        "proposal applies");
    require(newVersion == 2, "version bumped");
    require(backend.canUndo(), "applied proposal is undoable");
    backend.undo();
    require(backend.read({mqt::core::ByteRange{0, 17}}) == "line1\nline2\nline3",
        "undo restores pre-proposal content");
    backend.redo();

    // 版本冲突：旧提案拒绝
    mqt::ai::EditProposal stale = proposal;
    stale.baseVersion = 1; // 旧版本
    auto check2 = mqt::ai::EditProposalEvaluator::validate(stale, backend);
    require(check2 == mqt::ai::ProposalCheck::StaleVersion, "stale proposal rejected");

    // 指纹不匹配：内容基线被替换
    mqt::ai::EditProposal wrongFingerprint = proposal;
    wrongFingerprint.baseVersion = backend.snapshot().version;
    wrongFingerprint.baseFingerprint = 12345;
    auto check3 = mqt::ai::EditProposalEvaluator::validate(wrongFingerprint, backend);
    require(check3 == mqt::ai::ProposalCheck::FingerprintMismatch,
        "fingerprint mismatch rejected");

    // 影响预览 Diff
    proposal.baseVersion = backend.snapshot().version;
    proposal.baseFingerprint = backend.fingerprint();
    proposal.edits.clear();
    proposal.edits.push_back({6, 11, "LINE TWO"});
    const auto diff = mqt::ai::EditProposalEvaluator::impactDiff(proposal, backend);
    require(!diff.hunks().empty(), "impact diff shows the change");

    std::filesystem::remove(path);
}

} // namespace

int main()
{
    try {
        testProposalLifecycle();
        std::cout << "edit proposal tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
