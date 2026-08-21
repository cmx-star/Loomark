#pragma once

#include "core/byte_range.h"

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
};

struct BuildPreviewOptions {
    std::size_t chunkSize = 1024 * 1024;
    std::size_t maxBlocks = 10000;
    std::size_t maxLineBytes = 1024 * 1024;
    bool collectParagraphs = true;
};

struct PreviewIndex {
    std::vector<MarkdownBlock> blocks;
    std::uint64_t bytesScanned = 0;
    bool truncated = false;
};

[[nodiscard]] std::string_view toString(MarkdownBlockType type);
[[nodiscard]] PreviewIndex buildPreviewIndex(const std::filesystem::path& path, const BuildPreviewOptions& options = {});

} // namespace mqt::core
