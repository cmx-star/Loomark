#include "core/view_sync.h"

#include <algorithm>

namespace mqt::core {

bool ModeStateMachine::transitionAllowed(ViewMode from, ViewMode to)
{
    if (from == to) {
        return false;
    }
    // 所有模式间迁移都允许（模式与面板解耦；非法组合由面板状态管理）
    switch (to) {
    case ViewMode::Code:
    case ViewMode::Preview:
    case ViewMode::Split:
    case ViewMode::ParallelPreview:
    case ViewMode::Diff:
        return true;
    }
    return false;
}

bool ModeStateMachine::transitionTo(ViewMode target)
{
    if (!transitionAllowed(mode_, target)) {
        return false;
    }
    mode_ = target;
    return true;
}

ScrollSync::Anchor ScrollSync::anchorFor(std::uint64_t sourceOffset,
    std::uint64_t docLength, const std::string& headingPath)
{
    Anchor a;
    a.sourceOffset = sourceOffset;
    a.headingPath = headingPath;
    a.ratio = docLength == 0 ? 0.0 : double(sourceOffset) / double(docLength);
    return a;
}

bool ScrollSync::applyAnchor(const Anchor& anchor)
{
    if (syncing_) {
        return false; // 回环抑制：正在同步的一方忽略反向请求
    }
    lastApplied_ = anchor.sourceOffset;
    return true;
}

void DiffModel::compute(std::string_view baseline, std::string_view current)
{
    hunks_.clear();
    computed_ = true;

    auto splitLines = [](std::string_view text) {
        std::vector<std::string> lines;
        std::size_t start = 0;
        while (start <= text.size()) {
            if (start == text.size()) {
                break;
            }
            const auto nl = text.find('\n', start);
            if (nl == std::string_view::npos) {
                lines.emplace_back(text.substr(start));
                break;
            }
            lines.emplace_back(text.substr(start, nl - start));
            start = nl + 1;
        }
        return lines;
    };

    const auto oldLines = splitLines(baseline);
    const auto newLines = splitLines(current);

    // 经典 LCS DP（行数上限保护：超长文档仅取前 20000 行参与 diff，
    // 上限之外的尾行合并为一个 hunk；完整算法在后续批次替换为 Myers 分段）
    constexpr std::size_t kMaxDiffLines = 20000;
    const std::size_t n = std::min(oldLines.size(), kMaxDiffLines);
    const std::size_t m = std::min(newLines.size(), kMaxDiffLines);
    std::vector<std::vector<std::uint32_t>> dp(n + 1, std::vector<std::uint32_t>(m + 1, 0));
    for (std::size_t i = n; i-- > 0;) {
        for (std::size_t j = m; j-- > 0;) {
            dp[i][j] = oldLines[i] == newLines[j]
                ? dp[i + 1][j + 1] + 1
                : std::max(dp[i + 1][j], dp[i][j + 1]);
        }
    }

    std::size_t i = 0, j = 0;
    while (i < n && j < m) {
        if (oldLines[i] == newLines[j]) {
            ++i; ++j;
            continue;
        }
        const std::uint64_t delStart = i, newStart = j;
        std::uint64_t delEnd = i, newEnd = j;
        while (i < n && (j >= m || dp[i + 1][j] >= dp[i][j + 1]) && oldLines[i] != newLines[j]) {
            ++i; ++delEnd;
        }
        while (j < m && (i >= n || dp[i][j + 1] >= dp[i + 1][j]) && (i >= n || oldLines[i] != newLines[j])) {
            ++j; ++newEnd;
        }
        hunks_.push_back(DiffHunk{delStart, delEnd - delStart, newStart, newEnd - newStart});
    }
    if (i < oldLines.size() || j < newLines.size()) {
        hunks_.push_back(DiffHunk{i, oldLines.size() - i, j, newLines.size() - j});
    }
}

} // namespace mqt::core
