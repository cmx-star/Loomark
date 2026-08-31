#pragma once

#include "core/byte_range.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mqt::core {

enum class MarkdownBlockType {
    Heading,
    Paragraph,
    CodeFence
};

struct MarkdownBlock {
    std::uint64_t id = 0;
    MarkdownBlockType type = MarkdownBlockType::Paragraph;
    ByteRange sourceRange;
    std::uint8_t headingLevel = 0;
    std::string text;
    /// True when block.text was cut short by maxBlockTextBytes; the full line
    /// range is still available in sourceRange.
    bool textTruncated = false;
};

struct BuildPreviewOptions {
    std::size_t chunkSize = 1024 * 1024;
    std::size_t maxBlocks = 10000;
    std::size_t maxLineBytes = 1024 * 1024;
    bool collectParagraphs = true;
    /// Upper bound for the text captured per block so indexing a huge file
    /// stays memory-bounded. Set to 0 to disable text capture entirely.
    std::size_t maxBlockTextBytes = 8 * 1024;
    /// Optional cooperative cancellation flag checked while scanning.
    const std::atomic_bool* cancelFlag = nullptr;
};

struct PreviewIndex {
    std::vector<MarkdownBlock> blocks;
    std::uint64_t bytesScanned = 0;
    bool truncated = false;
    bool cancelled = false;
};

[[nodiscard]] std::string_view toString(MarkdownBlockType type);
[[nodiscard]] PreviewIndex buildPreviewIndex(const std::filesystem::path& path, const BuildPreviewOptions& options = {});

} // namespace mqt::core
