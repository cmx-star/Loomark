#pragma once

#include "core/markdown_block.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mqt::core {

/// M25 虚拟预览模型：轻量块描述 + 可视窗口 + 预加载带。
/// 只保存块元数据，不创建渲染对象；消费方按可视窗口实例化。
class VirtualPreviewModel {
public:
    struct Item {
        BlockId blockId = kInvalidBlockId;
        BlockKind kind = BlockKind::Paragraph;
        ByteRange sourceRange{};
        std::uint64_t approxHeight = 0; // 估算像素高（按字节数/行数粗估）
        bool oversized = false;         // M26：超过 kOversizedBlockBytes 需二次虚拟化
    };

    void rebuild(const MarkdownBlockIndex& index, std::uint64_t contentLength);

    [[nodiscard]] std::size_t itemCount() const { return items_.size(); }
    /// 可视窗口：返回覆盖 [viewOffset, viewOffset+viewSpan) 的条目索引区间，
    /// 含 preloadBand 条预加载。复杂度 O(log n + 可见数)。
    struct Window {
        std::size_t first = 0;
        std::size_t last = 0; // 含端
        bool valid = false;
    };
    [[nodiscard]] Window visibleWindow(std::uint64_t viewOffset,
        std::uint64_t viewSpan, std::size_t preloadBand = 3) const;

    [[nodiscard]] const Item& item(std::size_t index) const { return items_[index]; }
    [[nodiscard]] std::uint64_t totalHeight() const { return totalHeight_; }

    static constexpr std::uint64_t kOversizedBlockBytes = 256ULL << 10; // 256 KiB

private:
    std::vector<Item> items_;
    std::vector<std::uint64_t> prefixHeight_; // 前缀高度和（二分定位）
    std::uint64_t totalHeight_ = 0;
};

/// M26 大块二次虚拟化：把超大块切成固定大小的子窗口。
class OversizedBlockSplitter {
public:
    struct SubWindow {
        ByteRange sourceRange{};
        std::uint64_t index = 0;
        std::uint64_t count = 0;
    };
    [[nodiscard]] static std::vector<SubWindow> split(ByteRange block,
        std::uint64_t subWindowSize = 64ULL << 10);
};

/// M27 目录模型：标题大纲（层级 + 源码范围 + 稳定序号）。
class OutlineModel {
public:
    struct Entry {
        int level = 1;
        std::string text;      // 去掉 # 前缀的标题文本
        ByteRange sourceRange{};
        std::size_t sequence = 0; // 大纲内稳定序号
    };
    void rebuild(const MarkdownBlockIndex& index);
    [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }
    [[nodiscard]] std::size_t entryCount() const { return entries_.size(); }

private:
    std::vector<Entry> entries_;
};

/// M28 预览↔源码定位锚点。
class SourceLocator {
public:
    void rebuild(const MarkdownBlockIndex& index, std::uint64_t contentLength);

    /// 源码偏移 → 所在块 + 块内比例 [0,1]。
    struct Anchor {
        BlockId blockId = kInvalidBlockId;
        double ratioInBlock = 0.0;
        std::uint64_t sourceOffset = 0;
    };
    [[nodiscard]] Anchor anchorForOffset(std::uint64_t offset) const;
    /// 块 + 比例 → 源码偏移。
    [[nodiscard]] std::uint64_t offsetForAnchor(BlockId blockId, double ratio) const;

private:
    struct Span {
        BlockId blockId;
        std::uint64_t start = 0;
        std::uint64_t end = 0;
    };
    std::vector<Span> spans_; // 按 start 升序，不重叠
    std::uint64_t contentLength_ = 0;
};

} // namespace mqt::core
