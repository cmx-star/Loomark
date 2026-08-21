#include "core/document_file.h"
#include "core/file_tier.h"
#include "core/markdown_index.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path testRoot()
{
    auto root = std::filesystem::temp_directory_path() / "markdown_qt_core_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void writeText(const std::filesystem::path& path, std::string_view text, std::ios::openmode mode = std::ios::binary | std::ios::trunc)
{
    std::ofstream output(path, mode);
    output << text;
}

void testFileTierBoundaries()
{
    using namespace mqt::core;
    require(classifyDesktopFile(kNormalFileLimit) == FileTier::Normal, "64 MiB must be normal");
    require(classifyDesktopFile(kNormalFileLimit + 1) == FileTier::Large, "64 MiB + 1 must be large");
    require(classifyDesktopFile(kLargeFileLimit) == FileTier::Large, "256 MiB must be large");
    require(classifyDesktopFile(kLargeFileLimit + 1) == FileTier::Extreme, "256 MiB + 1 must be extreme");
    require(classifyDesktopFile(kExtremeFileLimit) == FileTier::Extreme, "512 MiB must be extreme");
    require(classifyDesktopFile(kExtremeFileLimit + 1) == FileTier::Reject, "512 MiB + 1 must be rejected");
}

void testInspectAndReadRange(const std::filesystem::path& root)
{
    const auto path = root / "mixed.md";
    writeText(path, "\xEF\xBB\xBF# Title\r\nbody\nlast");

    const auto info = mqt::core::inspectFile(path);
    require(info.hasUtf8Bom, "UTF-8 BOM should be detected");
    require(info.newlineStyle == mqt::core::NewlineStyle::Mixed, "mixed newline style should be detected");
    require(mqt::core::readRange(path, {3, 10}) == "# Title", "readRange should return exact bytes");
}

void testAtomicWrite(const std::filesystem::path& root)
{
    const auto path = root / "atomic.md";
    mqt::core::writeFileAtomically(path, "first");
    require(mqt::core::readRange(path, {0, 5}) == "first", "atomic write should create content");
    mqt::core::writeFileAtomically(path, "second");
    require(mqt::core::readRange(path, {0, 6}) == "second", "atomic write should replace content");
}

void testSearchLiteral(const std::filesystem::path& root)
{
    const auto path = root / "search.md";
    writeText(path,
        "alpha beta\n"
        "needle in the middle\n"
        "end of needle\n"
        "chunk-boundary-");
    writeText(path, "\x20needle\n", std::ios::binary | std::ios::app);

    mqt::core::SearchOptions options;
    options.chunkSize = 8;
    options.maxResults = 10;
    const auto result = mqt::core::searchLiteral(path, "needle", options);
    require(result.hits.size() == 3, "search should find all literal matches");
    require(result.hits[0].position.line == 2, "first match should be on line 2");
    require(result.hits[0].position.column == 1, "first match should start at column 1");
    require(result.hits[1].position.line == 3, "second match should be on line 3");
    require(result.hits[2].position.line == 4, "third match should cross chunk boundary and be found");
    require(result.hits[2].sourceRange.end > result.hits[2].sourceRange.start, "search result should expose byte range");
}

void testLocateByteRange(const std::filesystem::path& root)
{
    const auto path = root / "locate.md";
    writeText(path, "\xEF\xBB\xBF# Title\r\nbody\nlast");
    const auto result = mqt::core::locateByteRange(path, {3, 10});
    require(result.start.line == 1, "range start should be on first line");
    require(result.start.column == 1, "range start should begin after BOM");
    require(result.end.line == 1, "range end should stay on first line");
    require(result.end.column == 8, "range end should end after the title bytes");
}

void testPreviewIndex(const std::filesystem::path& root)
{
    const auto path = root / "index.md";
    writeText(path,
        "# One\n"
        "\n"
        "paragraph line 1\n"
        "paragraph line 2\n"
        "\n"
        "```md\n"
        "# Not a heading\n"
        "```\n"
        "## Two\n");

    const auto index = mqt::core::buildPreviewIndex(path);
    require(index.blocks.size() == 4, "preview index should contain heading, paragraph, code fence, heading");
    require(index.blocks[0].type == mqt::core::MarkdownBlockType::Heading, "first block should be heading");
    require(index.blocks[0].headingLevel == 1, "first heading should be level 1");
    require(index.blocks[0].text == "One", "first heading text should be parsed");
    require(index.blocks[1].type == mqt::core::MarkdownBlockType::Paragraph, "second block should be paragraph");
    require(index.blocks[2].type == mqt::core::MarkdownBlockType::CodeFence, "third block should be code fence");
    require(index.blocks[3].text == "Two", "fenced heading must not leak into outline");
}

void testPreviewTruncation(const std::filesystem::path& root)
{
    const auto path = root / "truncate.md";
    writeText(path, "# A\n# B\n# C\n");
    mqt::core::BuildPreviewOptions options;
    options.maxBlocks = 2;
    const auto index = mqt::core::buildPreviewIndex(path, options);
    require(index.blocks.size() == 2, "preview index should stop at max blocks");
    require(index.truncated, "preview index should mark truncation");
    require(index.bytesScanned > 0, "truncated preview index should retain scan progress");
}

} // namespace

int main()
{
    try {
        const auto root = testRoot();
        testFileTierBoundaries();
        testInspectAndReadRange(root);
        testAtomicWrite(root);
        testSearchLiteral(root);
        testLocateByteRange(root);
        testPreviewIndex(root);
        testPreviewTruncation(root);
        std::filesystem::remove_all(root);
        std::cout << "core tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
