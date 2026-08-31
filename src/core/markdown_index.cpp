#include "core/markdown_index.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace mqt::core {
namespace {

std::string_view trimRight(std::string_view text)
{
    while (!text.empty() && (text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

std::string_view trim(std::string_view text)
{
    text = trimRight(text);
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    return text;
}

std::size_t countIndent(std::string_view text)
{
    std::size_t indent = 0;
    for (char ch : text) {
        if (ch == ' ') {
            ++indent;
        } else {
            break;
        }
    }
    return indent;
}

bool isBlank(std::string_view text)
{
    return trim(text).empty();
}

bool isFenceLine(std::string_view line)
{
    const auto indent = countIndent(line);
    if (indent > 3) {
        return false;
    }
    line.remove_prefix(std::min(indent, line.size()));
    line = trimRight(line);
    return line.starts_with("```") || line.starts_with("~~~");
}

bool parseHeading(std::string_view line, std::uint8_t& level, std::string& text)
{
    const auto indent = countIndent(line);
    if (indent > 3) {
        return false;
    }
    line.remove_prefix(std::min(indent, line.size()));

    std::uint8_t count = 0;
    while (count < 6 && count < line.size() && line[count] == '#') {
        ++count;
    }
    if (count == 0) {
        return false;
    }
    if (count < line.size() && line[count] != ' ' && line[count] != '\t') {
        return false;
    }

    line.remove_prefix(count);
    line = trim(line);
    while (!line.empty() && line.back() == '#') {
        line.remove_suffix(1);
    }
    line = trim(line);

    level = count;
    text.assign(line.begin(), line.end());
    return true;
}

bool pushBlock(PreviewIndex& index, MarkdownBlock block, const BuildPreviewOptions& options)
{
    if (index.blocks.size() >= options.maxBlocks) {
        index.truncated = true;
        return false;
    }
    block.id = static_cast<std::uint64_t>(index.blocks.size() + 1);
    index.blocks.push_back(std::move(block));
    return true;
}

} // namespace

std::string_view toString(MarkdownBlockType type)
{
    switch (type) {
    case MarkdownBlockType::Heading:
        return "heading";
    case MarkdownBlockType::Paragraph:
        return "paragraph";
    case MarkdownBlockType::CodeFence:
        return "code_fence";
    }
    return "unknown";
}

PreviewIndex buildPreviewIndex(const std::filesystem::path& path, const BuildPreviewOptions& options)
{
    if (options.chunkSize == 0 || options.maxLineBytes == 0) {
        throw std::invalid_argument("preview index options must be non-zero");
    }

    const auto cancelled = [&options] {
        return options.cancelFlag != nullptr && options.cancelFlag->load(std::memory_order_relaxed);
    };

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    PreviewIndex index;
    std::string buffer(options.chunkSize, '\0');
    std::string line;
    line.reserve(std::min<std::size_t>(4096, options.maxLineBytes));

    bool lineTruncated = false;
    bool inFence = false;
    std::uint64_t offset = 0;
    std::uint64_t lineStart = 0;
    std::uint64_t paragraphStart = 0;
    bool paragraphOpen = false;
    std::uint64_t fenceStart = 0;

    // Appends one whole line to a block text buffer, keeping the total under
    // options.maxBlockTextBytes. All-or-nothing per line keeps UTF-8 intact.
    auto appendBlockLine = [&options](std::string& target, std::string_view content, bool& truncated) {
        if (truncated || options.maxBlockTextBytes == 0) {
            return;
        }
        const std::size_t needed = target.size() + (target.empty() ? 0 : 1) + content.size();
        if (needed > options.maxBlockTextBytes) {
            truncated = true;
            return;
        }
        if (!target.empty()) {
            target.push_back('\n');
        }
        target.append(content.begin(), content.end());
    };

    bool paragraphTextTruncated = false;
    bool fenceTextTruncated = false;
    std::string paragraphText;
    std::string fenceText;

    auto closeParagraph = [&](std::uint64_t end) -> bool {
        if (!paragraphOpen || !options.collectParagraphs) {
            paragraphOpen = false;
            return true;
        }
        paragraphOpen = false;
        MarkdownBlock block;
        block.type = MarkdownBlockType::Paragraph;
        block.sourceRange = {paragraphStart, end};
        block.text = std::move(paragraphText);
        block.textTruncated = paragraphTextTruncated;
        paragraphText.clear();
        paragraphTextTruncated = false;
        return pushBlock(index, std::move(block), options);
    };

    auto processLine = [&](std::uint64_t start, std::uint64_t end) -> bool {
        index.bytesScanned = end;
        if (lineTruncated) {
            lineTruncated = false;
            if (inFence) {
                fenceTextTruncated = true;
                return true;
            }
            if (!paragraphOpen) {
                paragraphOpen = true;
                paragraphStart = start;
            }
            paragraphTextTruncated = true;
            return true;
        }

        std::string_view view(line);
        view = trimRight(view);

        if (inFence) {
            if (isFenceLine(view)) {
                inFence = false;
                MarkdownBlock block;
                block.type = MarkdownBlockType::CodeFence;
                block.sourceRange = {fenceStart, end};
                block.text = std::move(fenceText);
                block.textTruncated = fenceTextTruncated;
                fenceText.clear();
                fenceTextTruncated = false;
                if (!pushBlock(index, std::move(block), options)) {
                    return false;
                }
            } else {
                appendBlockLine(fenceText, view, fenceTextTruncated);
            }
            return true;
        }

        if (isFenceLine(view)) {
            if (!closeParagraph(start)) {
                return false;
            }
            inFence = true;
            fenceStart = start;
            fenceText.clear();
            fenceTextTruncated = false;
            return true;
        }

        if (isBlank(view)) {
            return closeParagraph(start);
        }

        std::uint8_t headingLevel = 0;
        std::string headingText;
        if (parseHeading(view, headingLevel, headingText)) {
            if (!closeParagraph(start)) {
                return false;
            }
            MarkdownBlock block;
            block.type = MarkdownBlockType::Heading;
            block.sourceRange = {start, end};
            block.headingLevel = headingLevel;
            block.text = std::move(headingText);
            return pushBlock(index, std::move(block), options);
        }

        if (!paragraphOpen) {
            paragraphOpen = true;
            paragraphStart = start;
        }
        appendBlockLine(paragraphText, view, paragraphTextTruncated);
        return true;
    };

    while (input && !index.truncated && !index.cancelled) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = input.gcount();
        if (read <= 0) {
            break;
        }
        if (cancelled()) {
            index.cancelled = true;
            break;
        }

        for (std::streamsize i = 0; i < read; ++i) {
            const char ch = buffer[static_cast<std::size_t>(i)];
            const auto nextOffset = offset + 1;
            offset = nextOffset;

            if (ch == '\n') {
                if (!processLine(lineStart, nextOffset)) {
                    break;
                }
                line.clear();
                lineTruncated = false;
                lineStart = nextOffset;
            } else if (!lineTruncated) {
                if (line.size() < options.maxLineBytes) {
                    line.push_back(ch);
                } else {
                    line.clear();
                    lineTruncated = true;
                }
            }
        }
    }

    if (!index.truncated && !index.cancelled && (lineStart < offset || !line.empty())) {
        processLine(lineStart, offset);
    }
    if (!index.truncated && !index.cancelled && inFence) {
        MarkdownBlock block;
        block.type = MarkdownBlockType::CodeFence;
        block.sourceRange = {fenceStart, offset};
        block.text = std::move(fenceText);
        block.textTruncated = fenceTextTruncated;
        pushBlock(index, std::move(block), options);
    }
    if (!index.truncated && !index.cancelled) {
        closeParagraph(offset);
    }
    index.bytesScanned = offset;
    return index;
}

} // namespace mqt::core
