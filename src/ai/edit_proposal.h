#pragma once

#include "ai/ai_provider.h"
#include "core/document_backend.h"
#include "core/view_sync.h"

#include <QString>
#include <vector>

namespace mqt::ai {

/// M58 EditProposal：AI 提出的修改——不直接触碰正文。
/// 应用必须显式调用 applyProposal，并经过版本/指纹校验。
struct EditProposal {
    std::uint64_t baseVersion = 0;
    std::uint64_t baseFingerprint = 0;
    std::vector<mqt::core::TextEdit> edits;
    QString explanation;      // AI 给出的修改说明
    QString source;           // 来源标识（如 "ai:gpt-4o-mini"）
};

/// M60 校验结果。
enum class ProposalCheck {
    Ok,
    StaleVersion,        // 版本已前进 → 可显式重新生成
    FingerprintMismatch, // 内容基线不匹配 → 必须重算
    Overlap,             // 编辑区间重叠
    Empty,
};

/// M59/M61 提案评估与应用。
class EditProposalEvaluator {
public:
    /// 校验提案是否仍可应用（不修改文档）。
    [[nodiscard]] static ProposalCheck validate(
        const EditProposal& proposal, mqt::core::IDocumentBackend& backend);

    /// M61 应用：单次撤销组、失败保持原文、成功后可撤销。
    /// 返回 false 时文档内容与版本均未改变。
    [[nodiscard]] static bool applyProposal(const EditProposal& proposal,
        mqt::core::IDocumentBackend& backend, std::uint64_t* newVersion,
        QString* error);

    /// M59 影响预览：计算提案将产生的行级差异（hunks）。
    [[nodiscard]] static mqt::core::DiffModel impactDiff(
        const EditProposal& proposal, mqt::core::IDocumentBackend& backend);
};

} // namespace mqt::ai
