#include "core/document_file.h"
#include "core/file_tier.h"
#include "core/markdown_index.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
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
        "code body\n"
        "```\n"
        "## Two\n");

    const auto index = mqt::core::buildPreviewIndex(path);
    require(index.blocks.size() == 4, "preview index should contain heading, paragraph, code fence, heading");
    require(index.blocks[0].type == mqt::core::MarkdownBlockType::Heading, "first block should be heading");
    require(index.blocks[0].headingLevel == 1, "first heading should be level 1");
    require(index.blocks[0].text == "One", "first heading text should be parsed");
    require(index.blocks[1].type == mqt::core::MarkdownBlockType::Paragraph, "second block should be paragraph");
    require(index.blocks[1].text == "paragraph line 1\nparagraph line 2", "paragraph text must be captured for preview");
    require(!index.blocks[1].textTruncated, "short paragraph text must not be marked truncated");
    require(index.blocks[2].type == mqt::core::MarkdownBlockType::CodeFence, "third block should be code fence");
    require(index.blocks[2].text == "# Not a heading\ncode body", "code fence body text must be captured for preview");
    require(index.blocks[3].text == "Two", "fenced heading must not leak into outline");
}

void testPreviewTextCapAndCancel(const std::filesystem::path& root)
{
    const auto path = root / "cap_cancel.md";
    std::string longLine(200, 'x');
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        for (int i = 0; i < 100; ++i) {
            output << longLine << "\n";
        }
    }

    mqt::core::BuildPreviewOptions options;
    options.maxBlockTextBytes = 256;
    const auto index = mqt::core::buildPreviewIndex(path, options);
    require(index.blocks.size() == 1, "consecutive lines form a single paragraph block");
    require(index.blocks[0].textTruncated, "paragraph text beyond the cap must be marked truncated");
    require(index.blocks[0].text.size() <= 256, "paragraph text must respect maxBlockTextBytes");
    std::filesystem::remove(path);

    // A pre-set cancel flag must stop the scan before any work is done.
    const auto bigPath = root / "cancel_big.md";
    {
        std::ofstream output(bigPath, std::ios::binary | std::ios::trunc);
        for (int i = 0; i < 1000; ++i) {
            output << longLine << "\n";
        }
    }
    std::atomic_bool cancel {true};
    options.maxBlockTextBytes = 8 * 1024;
    options.cancelFlag = &cancel;
    const auto cancelledIndex = mqt::core::buildPreviewIndex(bigPath, options);
    require(cancelledIndex.cancelled, "preview index must honour the cancel flag");
    require(cancelledIndex.blocks.empty(), "cancelled preview index should carry no blocks");
    options.cancelFlag = nullptr;

    const auto inspectPath = root / "cancel_inspect.md";
    writeText(inspectPath, "a\r\nb\r\n");
    std::atomic_bool inspectCancel {true};
    const auto partial = mqt::core::inspectFile(inspectPath, &inspectCancel);
    require(!partial.newlineStyleKnown, "cancelled inspection must leave newline style undetermined");
    std::atomic_bool inspectGo {false};
    const auto full = mqt::core::inspectFile(inspectPath, &inspectGo);
    require(full.newlineStyleKnown && full.newlineStyle == mqt::core::NewlineStyle::CRLF,
        "uncancelled inspection should classify CRLF");
    std::filesystem::remove(bigPath);
}

void testAtomicFileWriter(const std::filesystem::path& root)
{
    const auto path = root / "streamed.md";
    {
        mqt::core::AtomicFileWriter writer(path);
        writer.write("head-");
        writer.write("tail");
        writer.commit();
    }
    require(mqt::core::readRange(path, {0, 9}) == "head-tail", "committed writer content must be visible");

    bool abortedCleanly = false;
    try {
        mqt::core::AtomicFileWriter aborter(path);
        aborter.write("partial");
        throw std::runtime_error("boom");
    } catch (const std::exception&) {
        abortedCleanly = true;
    }
    require(abortedCleanly, "aborted writer should surface its error");
    require(mqt::core::readRange(path, {0, 9}) == "head-tail", "aborted writer must not corrupt the target");
    bool tempGone = true;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.path().filename().string().rfind("streamed.md.tmp.", 0) == 0) {
            tempGone = false;
        }
    }
    require(tempGone, "aborted writer must remove its temp file");

    const auto replaceTarget = root / "replace-target";
    std::filesystem::create_directory(replaceTarget);
    bool replaceFailed = false;
    try {
        mqt::core::AtomicFileWriter writer(replaceTarget);
        writer.write("must-not-replace-directory");
        writer.commit();
    } catch (const std::exception&) {
        replaceFailed = true;
    }
    require(replaceFailed, "replacing an existing directory must fail");
    require(std::filesystem::is_directory(replaceTarget), "replace failure must preserve the target");
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        require(entry.path().filename().string().rfind("replace-target.tmp.", 0) != 0,
            "replace failure must remove the temp file");
    }
}

void testPreviewLongLineTruncation(const std::filesystem::path& root)
{
    const auto paragraphPath = root / "long-paragraph.md";
    writeText(paragraphPath, std::string(2048, 'x'));

    mqt::core::BuildPreviewOptions options;
    options.maxLineBytes = 128;
    options.maxBlockTextBytes = 64;
    const auto paragraphIndex = mqt::core::buildPreviewIndex(paragraphPath, options);
    require(paragraphIndex.blocks.size() == 1, "oversized paragraph line must remain represented");
    require(paragraphIndex.blocks[0].type == mqt::core::MarkdownBlockType::Paragraph,
        "oversized plain line must remain a paragraph block");
    require(paragraphIndex.blocks[0].textTruncated,
        "oversized paragraph line must report text truncation");

    const auto fencePath = root / "long-fence.md";
    writeText(fencePath, "```\n" + std::string(2048, 'y') + "\n```\n");
    const auto fenceIndex = mqt::core::buildPreviewIndex(fencePath, options);
    require(fenceIndex.blocks.size() == 1, "oversized fenced line must remain represented");
    require(fenceIndex.blocks[0].type == mqt::core::MarkdownBlockType::CodeFence,
        "oversized fenced line must remain a code fence block");
    require(fenceIndex.blocks[0].textTruncated,
        "oversized fenced line must report text truncation");

    const auto unclosedFencePath = root / "unclosed-fence.md";
    writeText(unclosedFencePath, "```text\nbody without closing fence\n");
    const auto unclosedFenceIndex = mqt::core::buildPreviewIndex(unclosedFencePath, options);
    require(unclosedFenceIndex.blocks.size() == 1,
        "unclosed fence must still produce a preview block at end of file");
    require(unclosedFenceIndex.blocks[0].type == mqt::core::MarkdownBlockType::CodeFence,
        "unclosed fence must retain its code fence type");
    require(unclosedFenceIndex.blocks[0].text == "body without closing fence",
        "unclosed fence preview must retain captured body text");
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

void testAvailableDiskBytes(const std::filesystem::path& root)
{
    const auto bytes = mqt::core::availableDiskBytes(root);
    require(bytes > 0, "existing directory should report positive free space");

    bool threw = false;
    try {
        (void)mqt::core::availableDiskBytes(root / "missing-dir" / "file.md");
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, "unresolvable path should throw instead of reporting unknown free space");
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
        testPreviewTextCapAndCancel(root);
        testAtomicFileWriter(root);
        testPreviewLongLineTruncation(root);
        testAvailableDiskBytes(root);
        std::filesystem::remove_all(root);
        std::cout << "core tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
