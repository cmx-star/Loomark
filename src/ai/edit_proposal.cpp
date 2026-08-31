#include "ai/edit_proposal.h"
#include "core/view_sync.h"

namespace mqt::ai {

ProposalCheck EditProposalEvaluator::validate(
    const EditProposal& proposal, mqt::core::IDocumentBackend& backend)
{
    if (proposal.edits.empty()) {
        return ProposalCheck::Empty;
    }
    if (proposal.baseVersion != backend.snapshot().version) {
        return ProposalCheck::StaleVersion;
    }
    if (proposal.baseFingerprint != backend.fingerprint()) {
        return ProposalCheck::FingerprintMismatch;
    }
    // 重叠检查（升序）
    std::uint64_t prevEnd = 0;
    for (const auto& edit : proposal.edits) {
        if (edit.start < prevEnd) {
            return ProposalCheck::Overlap;
        }
        prevEnd = edit.end;
    }
    return ProposalCheck::Ok;
}

bool EditProposalEvaluator::applyProposal(const EditProposal& proposal,
    mqt::core::IDocumentBackend& backend, std::uint64_t* newVersion,
    QString* error)
{
    const auto check = validate(proposal, backend);
    if (check != ProposalCheck::Ok) {
        if (error != nullptr) {
            switch (check) {
            case ProposalCheck::StaleVersion:
                *error = QStringLiteral("文档已变化，提案已过期，请重新生成");
                break;
            case ProposalCheck::FingerprintMismatch:
                *error = QStringLiteral("内容基线不匹配，提案需重算");
                break;
            case ProposalCheck::Overlap:
                *error = QStringLiteral("提案的编辑区间存在重叠");
                break;
            case ProposalCheck::Empty:
                *error = QStringLiteral("提案为空");
                break;
            default: break;
            }
        }
        return false;
    }
    auto edits = proposal.edits;
    const auto result = backend.apply(std::move(edits), proposal.baseVersion, nullptr);
    if (result.error != mqt::core::ApplyError::None) {
        if (error != nullptr) {
            *error = QStringLiteral("应用失败，原文未改变");
        }
        return false;
    }
    if (newVersion != nullptr) {
        *newVersion = result.newVersion;
    }
    return true;
}

mqt::core::DiffModel EditProposalEvaluator::impactDiff(
    const EditProposal& proposal, mqt::core::IDocumentBackend& backend)
{
    // 以当前文档为基线，模拟应用提案后的文本为新文本，计算差异
    mqt::core::DiffModel diff;
    const auto length = static_cast<std::uint64_t>(
        dynamic_cast<mqt::core::FileDocumentBackend*>(&backend) != nullptr
            ? 0 : 0);
    (void)length;
    // 通过后端读取全文与模拟后的全文做行级对比
    std::string baseline = backend.read(
        mqt::core::ByteRange{0, backend.info().sizeBytes});
    std::string current = baseline;
    // 降序应用编辑（与 apply 相同策略），得到模拟文本
    auto edits = proposal.edits;
    std::sort(edits.begin(), edits.end(),
        [](const mqt::core::TextEdit& a, const mqt::core::TextEdit& b) {
            return a.start > b.start;
        });
    for (const auto& edit : edits) {
        if (edit.end > edit.start && edit.end <= current.size()) {
            current.erase(edit.start, edit.end - edit.start);
        }
        if (!edit.newText.empty() && edit.start <= current.size()) {
            current.insert(edit.start, edit.newText);
        }
    }
    diff.compute(baseline, current);
    return diff;
}

} // namespace mqt::ai
