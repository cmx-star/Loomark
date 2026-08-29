#include "core/virtual_preview.h"

#include <algorithm>

namespace mqt::core {

void VirtualPreviewModel::rebuild(const MarkdownBlockIndex& index,
    std::uint64_t contentLength)
{
    items_.clear();
    prefixHeight_.clear();
    totalHeight_ = 0;

    std::uint64_t previousEnd = 0;
    for (const auto& block : index.blocks()) {
        // 块间空隙（空行等）也占位，保证窗口连续
        if (block.sourceRange.start > previousEnd) {
            Item gap;
            gap.sourceRange = {previousEnd, block.sourceRange.start};
            gap.approxHeight = std::uint64_t{20} * (gap.sourceRange.end - gap.sourceRange.start) / 40 + 8;
            gap.oversized = false;
            totalHeight_ += gap.approxHeight;
            prefixHeight_.push_back(totalHeight_);
            items_.push_back(std::move(gap));
        }
        Item item;
        item.blockId = block.blockId;
        item.kind = block.kind;
        item.sourceRange = block.sourceRange;
        const auto bytes = item.sourceRange.end - item.sourceRange.start;
        item.oversized = bytes > kOversizedBlockBytes;
        // 粗估：每 40 字节一行，每行 20px + 边距
        item.approxHeight = std::uint64_t{20} * (bytes / 40 + 1) + 8;
        totalHeight_ += item.approxHeight;
        prefixHeight_.push_back(totalHeight_);
        items_.push_back(std::move(item));
        previousEnd = block.sourceRange.end;
    }
    if (previousEnd < contentLength) {
        Item tail;
        tail.sourceRange = {previousEnd, contentLength};
        tail.approxHeight = 8;
        totalHeight_ += tail.approxHeight;
        prefixHeight_.push_back(totalHeight_);
        items_.push_back(std::move(tail));
    }
}

VirtualPreviewModel::Window VirtualPreviewModel::visibleWindow(
    std::uint64_t viewOffset, std::uint64_t viewSpan, std::size_t preloadBand) const
{
    Window w;
    if (items_.empty()) {
        return w;
    }
    // 按前缀高度近似映射视口（item i 占 [prefix[i-1], prefix[i])）
    std::size_t first = 0;
    std::size_t last = items_.size() - 1;
    // 二分找到第一个 end > viewOffset 的条目
    std::size_t lo = 0, hi = items_.size();
    while (lo + 1 < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (prefixHeight_[mid] > viewOffset) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    first = lo;
    std::size_t i = first;
    std::uint64_t covered = 0;
    while (i < items_.size() && covered < viewSpan) {
        covered += items_[i].approxHeight;
        last = i;
        ++i;
    }
    first = first > preloadBand ? first - preloadBand : 0;
    last = std::min(last + preloadBand, items_.size() - 1);
    w.first = first;
    w.last = last;
    w.valid = true;
    return w;
}

std::vector<OversizedBlockSplitter::SubWindow> OversizedBlockSplitter::split(
    ByteRange block, std::uint64_t subWindowSize)
{
    std::vector<SubWindow> out;
    const auto size = block.size();
    if (size == 0) {
        return out;
    }
    const auto count = (size + subWindowSize - 1) / subWindowSize;
    for (std::uint64_t i = 0; i < count; ++i) {
        const auto start = block.start + i * subWindowSize;
        const auto end = std::min(block.start + (i + 1) * subWindowSize, block.end);
        out.push_back(SubWindow{{start, end}, i, count});
    }
    return out;
}

void OutlineModel::rebuild(const MarkdownBlockIndex& index)
{
    entries_.clear();
    std::size_t sequence = 0;
    for (const auto& block : index.blocks()) {
        if (block.kind != BlockKind::Heading) {
            continue;
        }
        Entry entry;
        entry.level = std::clamp(block.level, 1, 6);
        entry.sourceRange = block.sourceRange;
        entry.sequence = sequence++;
        if (!block.headingPath.empty()) {
            std::string text = block.headingPath.back();
            std::size_t i = 0;
            while (i < text.size() && text[i] == '#') {
                ++i;
            }
            while (i < text.size() && text[i] == ' ') {
                ++i;
            }
            entry.text = text.substr(i);
        }
        entries_.push_back(std::move(entry));
    }
}

void SourceLocator::rebuild(const MarkdownBlockIndex& index, std::uint64_t contentLength)
{
    spans_.clear();
    contentLength_ = contentLength;
    std::uint64_t previousEnd = 0;
    for (const auto& block : index.blocks()) {
        if (block.sourceRange.start > previousEnd) {
            spans_.push_back({kInvalidBlockId, previousEnd, block.sourceRange.start});
        }
        spans_.push_back({block.blockId, block.sourceRange.start, block.sourceRange.end});
        previousEnd = block.sourceRange.end;
    }
    if (previousEnd < contentLength) {
        spans_.push_back({kInvalidBlockId, previousEnd, contentLength});
    }
}

SourceLocator::Anchor SourceLocator::anchorForOffset(std::uint64_t offset) const
{
    Anchor a;
    a.sourceOffset = offset;
    if (spans_.empty()) {
        return a;
    }
    std::size_t lo = 0, hi = spans_.size();
    while (lo + 1 < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (spans_[mid].start <= offset) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const auto& span = spans_[lo];
    a.blockId = span.blockId;
    const auto spanLen = span.end > span.start ? span.end - span.start : 1;
    a.ratioInBlock = double(offset - span.start) / double(spanLen);
    return a;
}

std::uint64_t SourceLocator::offsetForAnchor(BlockId blockId, double ratio) const
{
    for (const auto& span : spans_) {
        if (span.blockId == blockId && span.blockId != kInvalidBlockId) {
            const auto spanLen = span.end > span.start ? span.end - span.start : 1;
            if (ratio < 0) ratio = 0;
            if (ratio > 1) ratio = 1;
            return span.start + static_cast<std::uint64_t>(double(spanLen) * ratio);
        }
    }
    return contentLength_;
}

} // namespace mqt::core
