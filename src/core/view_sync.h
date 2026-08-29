#pragma once

#include "core/markdown_block.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mqt::core {

/// M29 模式状态机：CODE/PREVIEW/SPLIT/PARALLEL_PREVIEW/DIFF。
/// 非法迁移被拒绝（返回 false），状态与面板解耦。
enum class ViewMode { Code, Preview, Split, ParallelPreview, Diff };

class ModeStateMachine {
public:
    [[nodiscard]] ViewMode mode() const { return mode_; }
    bool transitionTo(ViewMode target);
    /// 面板状态与模式解耦：允许的组合查询
    [[nodiscard]] static bool transitionAllowed(ViewMode from, ViewMode to);

private:
    ViewMode mode_ = ViewMode::Code;
};

/// M30/M32 分屏同步：源码范围锚点 + 回环抑制。
/// 策略：SourceRange（精确）→ HeadingPath（次选）→ Proportional（兜底），
/// 置信度不足时停止跟随（返回 nullopt 语义 = 不跟随）。
class ScrollSync {
public:
    struct Anchor {
        std::uint64_t sourceOffset = 0;   // 源码字节偏移
        std::string headingPath;          // 所属标题路径（次选策略）
        double ratio = 0.0;               // 文档内比例（兜底策略）
    };
    enum class Strategy { SourceRange, HeadingPath, Proportional, None };

    /// 从源码滚动位置推导锚点。
    [[nodiscard]] static Anchor anchorFor(std::uint64_t sourceOffset,
        std::uint64_t docLength, const std::string& headingPath);
    /// 应用锚点到对侧视图：syncing=true 时抑制回环（忽略请求）。
    void setSyncing(bool syncing) { syncing_ = syncing; }
    [[nodiscard]] bool isSyncing() const { return syncing_; }
    [[nodiscard]] bool applyAnchor(const Anchor& anchor); // true = 接受并应用
    [[nodiscard]] std::uint64_t lastAppliedOffset() const { return lastApplied_; }

private:
    bool syncing_ = false;
    std::uint64_t lastApplied_ = 0;
};

/// M33 Diff 模型：磁盘基线 vs 当前缓冲的行级差异（简化 Myers）。
/// 不保留打开时完整原文——基线按需从后端读取；版本变化使旧 Diff 失效。
struct DiffHunk {
    std::uint64_t oldStart = 0; // 0 起行号
    std::uint64_t oldLines = 0;
    std::uint64_t newStart = 0;
    std::uint64_t newLines = 0;
};

class DiffModel {
public:
    /// 基线/当前按 '\n' 切行（不含行尾换行）。
    void compute(std::string_view baseline, std::string_view current);
    [[nodiscard]] const std::vector<DiffHunk>& hunks() const { return hunks_; }
    [[nodiscard]] std::uint64_t baselineVersion() const { return baselineVersion_; }
    void invalidate(std::uint64_t newBaselineVersion)
    {
        hunks_.clear();
        baselineVersion_ = newBaselineVersion;
    }
    [[nodiscard]] bool valid() const { return !hunks_.empty() || computed_; }

private:
    std::vector<DiffHunk> hunks_;
    std::uint64_t baselineVersion_ = 0;
    bool computed_ = false;
};

} // namespace mqt::core
