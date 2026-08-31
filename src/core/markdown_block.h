#pragma once

#include "core/byte_range.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mqt::core {

/// 统一块模型（M21）——目录、预览、Diff、AI 的共享语义单元。
enum class BlockKind {
    Heading,
    Paragraph,
    CodeFence,
    ListItem,
    Quote,
    Table,
    Html,
    HorizontalRule,
};

using BlockId = std::uint64_t;
inline constexpr BlockId kInvalidBlockId = 0;

struct MarkdownBlock {
    BlockId blockId = kInvalidBlockId;
    std::uint64_t documentVersion = 0;
    BlockKind kind = BlockKind::Paragraph;
    ByteRange sourceRange{};
    /// 标题层级（Heading 有效），围栏语言标记长度等扩展信息
    int level = 0;
    /// 标题路径：从文档根到本块的各级标题文本（Heading 为自身文本）
    std::vector<std::string> headingPath;
    /// 首行/整块文本的 64 位指纹（FNV-1a），用于增量比较
    std::uint64_t hash = 0;
    std::string payload; // 标题文本 / 列表标记等
};

/// M23 安全检查点：跨块语义（围栏、引用深度）的收敛锚点。
struct FenceCheckpoint {
    std::uint64_t offset = 0;   // 检查点所在字节偏移
    std::uint64_t line = 0;     // 检查点所在行
    bool insideFence = false;   // 该偏移处是否处于未闭合围栏内
    std::string fenceMarker;    // 围栏标记（``` 或 ~~~，用于匹配闭合）
};

/// M21/M22：块索引——对整篇内容做一次行级扫描产出块列表。
/// 自研轻量解析（不依赖 md4qt AST），块边界与 CommonMark 日常子集一致：
/// ATX 标题、围栏代码、段落、列表项、引用、表格（| 首行）、水平线、HTML 块。
class MarkdownBlockIndex {
public:
    /// 解析整篇内容。documentVersion 记入每个块。
    void build(std::string_view content, std::uint64_t documentVersion);

    [[nodiscard]] const std::vector<MarkdownBlock>& blocks() const { return blocks_; }
    [[nodiscard]] std::uint64_t version() const { return version_; }
    [[nodiscard]] std::size_t blockCount() const { return blocks_.size(); }

    /// 按字节偏移查找块（二分）。
    [[nodiscard]] const MarkdownBlock* blockAt(std::uint64_t offset) const;
    /// 提取标题路径（目录用）：仅 Heading 块的路径列表。
    [[nodiscard]] std::vector<std::pair<ByteRange, std::vector<std::string>>>
    headingOutline() const;

private:
    std::vector<MarkdownBlock> blocks_;
    std::uint64_t version_ = 0;
};

/// M23：行级扫描器，维护围栏/引用状态并产出检查点。
/// `feed` 可任意分块调用；`checkpoints()` 供增量重扫定位最近的收敛锚点。
class LineScanState {
public:
    void feed(std::string_view chunk, std::uint64_t chunkStartOffset);
    /// 文档结束调用：未闭合围栏按闭合处理并记录最终检查点。
    void finish();
    void processFenceLine(std::string_view line);

    [[nodiscard]] const std::vector<FenceCheckpoint>& checkpoints() const { return checkpoints_; }
    [[nodiscard]] bool insideFence() const { return insideFence_; }
    [[nodiscard]] std::uint64_t lineCount() const { return line_; }

    /// 从指定检查点向后重扫：返回该检查点（收敛锚点）。
    [[nodiscard]] static FenceCheckpoint nearestCheckpointBefore(
        const std::vector<FenceCheckpoint>& checkpoints, std::uint64_t offset);

private:
    void recordCheckpoint(std::uint64_t offset);

    std::vector<FenceCheckpoint> checkpoints_;
    bool insideFence_ = false;
    std::string fenceMarker_;
    std::uint64_t line_ = 0;
    std::uint64_t newlines_ = 0;
    std::uint64_t offset_ = 0;
    std::string partial_; // 跨块携带的未完结行
    bool started_ = false;
    /// 每处理 256 行记录一个检查点
    static constexpr std::uint64_t kCheckpointStride = 256;
};

/// M24：版本化索引任务调度——只接受最新版本的结果。
class IndexScheduler {
public:
    /// 提交结果。返回 true 表示结果被接受（resultVersion == 最新请求版本）；
    /// 返回 false 表示结果过期，应丢弃（迟到结果）。
    bool submitResult(std::uint64_t resultVersion);
    /// 请求新版本：递增最新版本并返回之（旧任务应检查取消）。
    std::uint64_t requestNewVersion();
    [[nodiscard]] std::uint64_t latestVersion() const { return latest_; }
    [[nodiscard]] bool isCurrent(std::uint64_t version) const { return version == latest_; }
    void cancelAll() { cancelled_ = true; }
    [[nodiscard]] bool cancelled() const { return cancelled_; }

private:
    std::uint64_t latest_ = 1;
    bool cancelled_ = false;
};

/// FNV-1a 64 位（与 load_scanner 的 FingerprintSink 语义一致）。
[[nodiscard]] std::uint64_t blockHash(std::string_view text);

} // namespace mqt::core
