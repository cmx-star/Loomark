#include "core/document_backend.h"

#include <atomic>
#include <chrono>
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
    auto root = std::filesystem::temp_directory_path() / "markdown_qt_backend_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void writeText(const std::filesystem::path& path, std::string_view text,
    std::ios::openmode mode = std::ios::binary | std::ios::trunc)
{
    std::ofstream output(path, mode);
    output << text;
}

void testOpenSnapshot(const std::filesystem::path& root)
{
    const auto path = root / "basic.md";
    writeText(path, "# Hello\nworld\n");

    const auto backend = mqt::core::FileDocumentBackend(path);
    const auto snap = backend.snapshot();
    require(snap.version == mqt::core::kInitialDocumentVersion,
        "initial version must be 1");
    require(snap.info.tier == mqt::core::FileTier::Normal,
        "small file must be Normal tier");
    require(!snap.info.hasUtf8Bom,
        "file without BOM must report hasUtf8Bom=false");
    require(snap.info.sizeBytes == 14u,
        "sizeBytes must match content length without BOM");

    const auto info = backend.info();
    require(info.sizeBytes == 14u, "info() must match snapshot");
}

void testOpenSnapshotWithBom(const std::filesystem::path& root)
{
    const auto path = root / "bom.md";
    writeText(path, "\xEF\xBB\xBF# Title\r\nbody\nlast");

    const auto backend = mqt::core::FileDocumentBackend(path);
    const auto snap = backend.snapshot();
    require(snap.info.hasUtf8Bom,
        "BOM presence must be detected");
    require(snap.info.sizeBytes == 18u,
        "sizeBytes must be content length without BOM");
    require(snap.version == mqt::core::kInitialDocumentVersion,
        "initial version must be 1 regardless of BOM");
}

void testReadCorrectness(const std::filesystem::path& root)
{
    const auto path = root / "read.md";
    writeText(path, "\xEF\xBB\xBF" "abc\ndef\nghi");

    const auto backend = mqt::core::FileDocumentBackend(path);
    require(backend.read({mqt::core::ByteRange{0, 3}}) == "abc",
        "read first 3 bytes after BOM must return abc");
    require(backend.read({mqt::core::ByteRange{3, 7}}) == "\ndef",
        "read across newline");
    require(backend.read({mqt::core::ByteRange{0, 11}}) == "abc\ndef\nghi",
        "read full body");
    require(backend.read({mqt::core::ByteRange{9, 9}}).empty(),
        "empty read at boundary must return empty string");
}

void testReadOutOfRange(const std::filesystem::path& root)
{
    const auto path = root / "read_oom.md";
    writeText(path, "hello");

    const auto backend = mqt::core::FileDocumentBackend(path);
    bool threw = false;
    try {
        (void)backend.read({mqt::core::ByteRange{0, 10}});
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw,
        "read exceeding content size must throw std::out_of_range");
}

void testLocateLinesBasic(const std::filesystem::path& root)
{
    const auto path = root / "locate.md";
    writeText(path, "line1\nline2\nline3");

    const auto backend = mqt::core::FileDocumentBackend(path);
    auto result = backend.locateLines({mqt::core::ByteRange{0, 5}});
    require(result.start.line == 1,
        "range start at byte 0 must be line 1");
    require(result.start.column == 1,
        "range start at byte 0 must be column 1");
    require(result.end.line == 1,
        "range end at byte 5 must be line 1");
    require(result.end.column == 6,
        "range end at byte 5 must be column 6 (after newline)");

    result = backend.locateLines({mqt::core::ByteRange{6, 11}});
    require(result.start.line == 2,
        "range start after first newline must be line 2");
    require(result.end.line == 2,
        "range end within second line must be line 2");
    require(result.end.column == 6,
        "range end at byte 11 must be column 6");
}

void testLocateLinesWithBom(const std::filesystem::path& root)
{
    const auto path = root / "locate_bom.md";
    writeText(path, "\xEF\xBB\xBF# Title\r\nbody\nlast");

    const auto backend = mqt::core::FileDocumentBackend(path);
    auto result = backend.locateLines({mqt::core::ByteRange{0, 7}});
    require(result.start.line == 1,
        "range start at byte 0 before BOM must be line 1 column 1");
    require(result.start.column == 1,
        "byte 0 before BOM must still be column 1");
    require(result.end.line == 1,
        "range end at byte 7 (# Title) must be line 1");
    require(result.end.column == 8,
        "range end at byte 7 (# Title) must be column 8");
}

void testLocateLinesOutOfRange(const std::filesystem::path& root)
{
    const auto path = root / "locate_oom.md";
    writeText(path, "hello");

    const auto backend = mqt::core::FileDocumentBackend(path);
    bool threw = false;
    try {
        (void)backend.locateLines({mqt::core::ByteRange{0, 100}});
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw,
        "locateLines beyond content size must throw std::out_of_range");
}

void testSearchMatch(const std::filesystem::path& root)
{
    const auto path = root / "search.md";
    writeText(path,
        "alpha beta\n"
        "needle in the middle\n"
        "end of needle\n"
        "chunk-boundary-");
    writeText(path, " needle\n", std::ios::binary | std::ios::app);

    const auto backend = mqt::core::FileDocumentBackend(path);
    mqt::core::SearchQuery query;
    query.needle = "needle";
    query.options.chunkSize = 8;
    query.options.maxResults = 10;

    const auto outcome = backend.search(query);
    require(!outcome.cancelled,
        "full search must not be cancelled");
    require(outcome.result.hits.size() == 3,
        "search must find 3 literal matches");
    require(outcome.result.hits[0].position.line == 2,
        "first match must be on line 2");
    require(outcome.result.hits[0].position.column == 1,
        "first match must be at column 1");
    require(outcome.result.hits[2].position.line == 4,
        "third match must cross chunk boundary and be found");
    require(outcome.result.hits[2].sourceRange.end > outcome.result.hits[2].sourceRange.start,
        "search result must expose valid byte range");
    require(outcome.result.bytesScanned > 0,
        "bytesScanned must reflect total scan progress");
}

void testSearchTruncated(const std::filesystem::path& root)
{
    const auto path = root / "search_trunc.md";
    std::string content;
    for (int i = 0; i < 100; ++i) {
        content += "needle ";
    }
    writeText(path, content);

    const auto backend = mqt::core::FileDocumentBackend(path);
    mqt::core::SearchQuery query;
    query.needle = "needle";
    query.options.chunkSize = 16;
    query.options.maxResults = 5;

    const auto outcome = backend.search(query);
    require(outcome.result.truncated,
        "search must report truncation when maxResults exceeded");
    require(outcome.result.hits.size() == 5,
        "search must return exactly maxResults hits");
}

void testSearchCancelled(const std::filesystem::path& root)
{
    const auto path = root / "search_cancel.md";
    std::string content(1024 * 1024, 'x');
    content += "needle";
    writeText(path, content);

    const auto backend = mqt::core::FileDocumentBackend(path);
    std::atomic_bool cancel{true};

    mqt::core::SearchQuery query;
    query.needle = "needle";
    query.options.chunkSize = 1;
    query.options.maxResults = 100;

    const auto outcome = backend.search(query, &cancel);
    require(outcome.cancelled,
        "search with pre-set cancel flag must return cancelled=true");
    require(outcome.result.hits.empty(),
        "cancelled search must have empty hits");
}

void testApplySingleEdit(const std::filesystem::path& root)
{
    const auto path = root / "apply.md";
    writeText(path, "hello world");

    auto backend = mqt::core::FileDocumentBackend(path);
    mqt::core::TextEdit edit;
    edit.start = 6;
    edit.end = 11;
    edit.newText = "beautiful";

    const auto result = backend.apply({edit}, mqt::core::kInitialDocumentVersion);
    require(result.error == mqt::core::ApplyError::None,
        "normal single edit must succeed");
    require(result.newVersion == 2,
        "apply must increment version to 2");

    const auto content = backend.read({mqt::core::ByteRange{0, 15}});
    require(content == "hello beautiful",
        "applied edit must be visible in read");
}

void testApplyMultipleEdits(const std::filesystem::path& root)
{
    const auto path = root / "apply_multi.md";
    writeText(path, "aa bb cc");

    auto backend = mqt::core::FileDocumentBackend(path);
    mqt::core::TextEdit e1;
    e1.start = 0; e1.end = 2; e1.newText = "XX";
    mqt::core::TextEdit e2;
    e2.start = 6; e2.end = 8; e2.newText = "YY";

    const auto result = backend.apply({e1, e2}, mqt::core::kInitialDocumentVersion);
    require(result.error == mqt::core::ApplyError::None,
        "non-overlapping multi-edit must succeed");
    require(result.newVersion == 2,
        "multi-edit must increment version");

    const auto content = backend.read({mqt::core::ByteRange{0, 8}});
    require(content == "XX bb YY",
        "multi-edit must produce combined content");
}

void testApplyStaleVersion(const std::filesystem::path& root)
{
    const auto path = root / "apply_stale.md";
    writeText(path, "hello");

    auto backend = mqt::core::FileDocumentBackend(path);
    mqt::core::TextEdit edit;
    edit.start = 0; edit.end = 5; edit.newText = "replaced";

    const auto r1 = backend.apply({edit}, 99);
    require(r1.error == mqt::core::ApplyError::StaleVersion,
        "apply with stale version must return StaleVersion");
    require(r1.newVersion == mqt::core::kInitialDocumentVersion,
        "stale apply must not change version");

    const auto content = backend.read({mqt::core::ByteRange{0, 5}});
    require(content == "hello",
        "stale apply must not modify content");
}

void testApplyOverlappingEdits(const std::filesystem::path& root)
{
    const auto path = root / "apply_overlap.md";
    writeText(path, "abcdefghij");

    auto backend = mqt::core::FileDocumentBackend(path);
    mqt::core::TextEdit e1;
    e1.start = 0; e1.end = 5; e1.newText = "XXXXX";
    mqt::core::TextEdit e2;
    e2.start = 3; e2.end = 8; e2.newText = "YYYYY";

    const auto result = backend.apply({e1, e2}, mqt::core::kInitialDocumentVersion);
    require(result.error == mqt::core::ApplyError::OverlappingEdits,
        "overlapping edits must return OverlappingEdits");
}

void testApplyRangeInvalid(const std::filesystem::path& root)
{
    const auto path = root / "apply_invalid.md";
    writeText(path, "hello");

    auto backend = mqt::core::FileDocumentBackend(path);
    mqt::core::TextEdit edit;
    edit.start = 3; edit.end = 100; edit.newText = "x";

    const auto result = backend.apply({edit}, mqt::core::kInitialDocumentVersion);
    require(result.error == mqt::core::ApplyError::RangeInvalid,
        "edit extending beyond content must return RangeInvalid");
}

void testSaveRoundTrip(const std::filesystem::path& root)
{
    const auto path = root / "save.md";
    writeText(path, "\xEF\xBB\xBF# Title\r\nbody\nlast");

    auto backend = mqt::core::FileDocumentBackend(path);
    backend.save(nullptr);

    const auto saved = mqt::core::readRange(path,
        {0, std::filesystem::file_size(path)});
    require(saved == "\xEF\xBB\xBF# Title\r\nbody\nlast",
        "save must round-trip BOM+CRLF content exactly");
}

void testSaveReplacesContent(const std::filesystem::path& root)
{
    const auto path = root / "save_replace.md";
    writeText(path, "original");

    auto backend = mqt::core::FileDocumentBackend(path);
    mqt::core::TextEdit edit;
    edit.start = 0; edit.end = 8; edit.newText = "modified";
    backend.apply({edit}, mqt::core::kInitialDocumentVersion);
    backend.save(nullptr);

    const auto saved = mqt::core::readRange(path,
        {0, std::filesystem::file_size(path)});
    require(saved == "modified",
        "save after apply must persist changes");
}

void testSaveToReadOnlyDir(const std::filesystem::path& root)
{
    const auto path = root / "save_ro.md";
    writeText(path, "content");

    auto backend = mqt::core::FileDocumentBackend(path);
    const auto badDir = root / "no-such-dir";
    std::filesystem::create_directories(badDir);
    std::filesystem::permissions(badDir,
        std::filesystem::perms::owner_all & ~std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);
    const auto badPath = badDir / "out.md";
    bool threw = false;
    try {
        backend.saveAs(badPath);
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw,
        "saveAs to read-only dir must throw");

    const auto orig = mqt::core::readRange(path,
        {0, std::filesystem::file_size(path)});
    require(orig == "content",
        "failed saveAs must not corrupt original file");
}

void testReload(const std::filesystem::path& root)
{
    const auto path = root / "reload.md";
    writeText(path, "v1");

    auto backend = mqt::core::FileDocumentBackend(path);
    require(backend.read({mqt::core::ByteRange{0, 2}}) == "v1",
        "initial read must return original content");

    writeText(path, "v2");

    const auto snap = backend.reload();
    require(snap.version == mqt::core::kInitialDocumentVersion + 1,
        "reload must increment version even without edits");
    require(backend.read({mqt::core::ByteRange{0, 2}}) == "v2",
        "reload must reflect external modification");
}

void testReloadNoChange(const std::filesystem::path& root)
{
    const auto path = root / "reload_nochange.md";
    writeText(path, "static");

    auto backend = mqt::core::FileDocumentBackend(path);
    const auto snap1 = backend.reload();
    const auto snap2 = backend.reload();
    require(snap2.version == snap1.version + 1,
        "reload without external change must still increment version");
}

void testApplyEditsAfterReload(const std::filesystem::path& root)
{
    const auto path = root / "apply_reload.md";
    writeText(path, "original");

    auto backend = mqt::core::FileDocumentBackend(path);
    writeText(path, "external");

    const auto snap = backend.reload();
    require(snap.version == mqt::core::kInitialDocumentVersion + 1,
        "reload version must reflect external change");

    mqt::core::TextEdit edit;
    edit.start = 0; edit.end = 8; edit.newText = "after-reload";
    const auto result = backend.apply({edit}, snap.version);
    require(result.error == mqt::core::ApplyError::None,
        "apply with reload snapshot version must succeed");
    require(result.newVersion == snap.version + 1,
        "apply after reload must increment version correctly");
}

} // namespace

int main()
{
    try {
        const auto root = testRoot();
        testOpenSnapshot(root);
        testOpenSnapshotWithBom(root);
        testReadCorrectness(root);
        testReadOutOfRange(root);
        testLocateLinesBasic(root);
        testLocateLinesWithBom(root);
        testLocateLinesOutOfRange(root);
        testSearchMatch(root);
        testSearchTruncated(root);
        testSearchCancelled(root);
        testApplySingleEdit(root);
        testApplyMultipleEdits(root);
        testApplyStaleVersion(root);
        testApplyOverlappingEdits(root);
        testApplyRangeInvalid(root);
        testSaveRoundTrip(root);
        testSaveReplacesContent(root);
        testSaveToReadOnlyDir(root);
        testReload(root);
        testReloadNoChange(root);
        testApplyEditsAfterReload(root);
        std::filesystem::remove_all(root);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
