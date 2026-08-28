// M09 切片 A：ScintillaDocumentBackend 契约测试
// 场景镜像 tests/document_backend_tests.cpp（FileDocumentBackend 语义逐一对齐），
// 另补 Scintilla 特有用例：外部缓冲变更推进版本、apply 单步撤销组。
#include "backend/scintilla_document_backend.h"

#include <ScintillaEditBase.h>

#include <QApplication>
#include <QElapsedTimer>

#include <atomic>
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
    auto root = std::filesystem::temp_directory_path() / "markdown_qt_scintilla_backend_tests";
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

// 每个用例独立 editor 实例，避免缓冲串扰
struct Fixture {
    ScintillaEditBase editor;

    Fixture()
    {
        editor.resize(400, 300);
        editor.show();
        QApplication::processEvents();
    }
};

void testOpenSnapshot(const std::filesystem::path& root)
{
    const auto path = root / "basic.md";
    writeText(path, "# Hello\nworld\n");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
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

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    const auto snap = backend.snapshot();
    require(snap.info.hasUtf8Bom, "BOM presence must be detected");
    require(snap.info.sizeBytes == 18u,
        "sizeBytes must be content length without BOM");
    require(snap.version == mqt::core::kInitialDocumentVersion,
        "initial version must be 1 regardless of BOM");
}

void testReadCorrectness(const std::filesystem::path& root)
{
    const auto path = root / "read.md";
    writeText(path, "\xEF\xBB\xBF" "abc\ndef\nghi");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
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

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    bool threw = false;
    try {
        (void)backend.read({mqt::core::ByteRange{0, 10}});
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "read exceeding content size must throw std::out_of_range");
}

void testLocateLinesBasic(const std::filesystem::path& root)
{
    const auto path = root / "locate.md";
    writeText(path, "line1\nline2\nline3");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    auto result = backend.locateLines({mqt::core::ByteRange{0, 5}});
    require(result.start.line == 1, "range start at byte 0 must be line 1");
    require(result.start.column == 1, "range start at byte 0 must be column 1");
    require(result.end.line == 1, "range end at byte 5 must be line 1");
    require(result.end.column == 6, "range end at byte 5 must be column 6");

    result = backend.locateLines({mqt::core::ByteRange{6, 11}});
    require(result.start.line == 2,
        "range start after first newline must be line 2");
    require(result.end.line == 2, "range end within second line must be line 2");
    require(result.end.column == 6, "range end at byte 11 must be column 6");
}

void testLocateLinesWithBom(const std::filesystem::path& root)
{
    const auto path = root / "locate_bom.md";
    writeText(path, "\xEF\xBB\xBF# Title\r\nbody\nlast");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    auto result = backend.locateLines({mqt::core::ByteRange{0, 7}});
    require(result.start.line == 1,
        "range start at byte 0 before BOM must be line 1 column 1");
    require(result.start.column == 1, "byte 0 before BOM must still be column 1");
    require(result.end.line == 1, "range end at byte 7 (# Title) must be line 1");
    require(result.end.column == 8, "range end at byte 7 (# Title) must be column 8");
}

void testLocateLinesOutOfRange(const std::filesystem::path& root)
{
    const auto path = root / "locate_oom.md";
    writeText(path, "hello");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    bool threw = false;
    try {
        (void)backend.locateLines({mqt::core::ByteRange{0, 100}});
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "locateLines beyond content size must throw std::out_of_range");
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

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    mqt::core::SearchQuery query;
    query.needle = "needle";
    query.options.chunkSize = 8;
    query.options.maxResults = 10;

    const auto outcome = backend.search(query);
    require(!outcome.cancelled, "full search must not be cancelled");
    require(outcome.result.hits.size() == 3, "search must find 3 literal matches");
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

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
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

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    std::atomic_bool cancel{true};

    mqt::core::SearchQuery query;
    query.needle = "needle";
    query.options.chunkSize = 1;
    query.options.maxResults = 100;

    const auto outcome = backend.search(query, &cancel);
    require(outcome.cancelled,
        "search with pre-set cancel flag must return cancelled=true");
    require(outcome.result.hits.empty(), "cancelled search must have empty hits");
}

void testApplySingleEdit(const std::filesystem::path& root)
{
    const auto path = root / "apply.md";
    writeText(path, "hello world");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    mqt::core::TextEdit edit;
    edit.start = 6;
    edit.end = 11;
    edit.newText = "beautiful";

    const auto result = backend.apply({edit}, mqt::core::kInitialDocumentVersion);
    require(result.error == mqt::core::ApplyError::None,
        "normal single edit must succeed");
    require(result.newVersion == 2, "apply must increment version to 2");

    const auto content = backend.read({mqt::core::ByteRange{0, 15}});
    require(content == "hello beautiful", "applied edit must be visible in read");
}

void testApplyMultipleEdits(const std::filesystem::path& root)
{
    const auto path = root / "apply_multi.md";
    writeText(path, "aa bb cc");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    mqt::core::TextEdit e1;
    e1.start = 0; e1.end = 2; e1.newText = "XX";
    mqt::core::TextEdit e2;
    e2.start = 6; e2.end = 8; e2.newText = "YY";

    const auto result = backend.apply({e1, e2}, mqt::core::kInitialDocumentVersion);
    require(result.error == mqt::core::ApplyError::None,
        "non-overlapping multi-edit must succeed");
    require(result.newVersion == 2, "multi-edit must increment version");

    const auto content = backend.read({mqt::core::ByteRange{0, 8}});
    require(content == "XX bb YY", "multi-edit must produce combined content");
}

void testApplyStaleVersion(const std::filesystem::path& root)
{
    const auto path = root / "apply_stale.md";
    writeText(path, "hello");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    mqt::core::TextEdit edit;
    edit.start = 0; edit.end = 5; edit.newText = "replaced";

    const auto r1 = backend.apply({edit}, 99);
    require(r1.error == mqt::core::ApplyError::StaleVersion,
        "apply with stale version must return StaleVersion");
    require(r1.newVersion == mqt::core::kInitialDocumentVersion,
        "stale apply must not change version");

    const auto content = backend.read({mqt::core::ByteRange{0, 5}});
    require(content == "hello", "stale apply must not modify content");
}

void testApplyOverlappingEdits(const std::filesystem::path& root)
{
    const auto path = root / "apply_overlap.md";
    writeText(path, "abcdefghij");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
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

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
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

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
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

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    mqt::core::TextEdit edit;
    edit.start = 0; edit.end = 8; edit.newText = "modified";
    backend.apply({edit}, mqt::core::kInitialDocumentVersion);
    backend.save(nullptr);

    const auto saved = mqt::core::readRange(path,
        {0, std::filesystem::file_size(path)});
    require(saved == "modified", "save after apply must persist changes");
}

void testSaveToReadOnlyDir(const std::filesystem::path& root)
{
    const auto path = root / "save_ro.md";
    writeText(path, "content");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
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
    require(threw, "saveAs to read-only dir must throw");

    const auto orig = mqt::core::readRange(path,
        {0, std::filesystem::file_size(path)});
    require(orig == "content", "failed saveAs must not corrupt original file");
}

void testReload(const std::filesystem::path& root)
{
    const auto path = root / "reload.md";
    writeText(path, "v1");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
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

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    const auto snap1 = backend.reload();
    const auto snap2 = backend.reload();
    require(snap2.version == snap1.version + 1,
        "reload without external change must still increment version");
}

void testApplyEditsAfterReload(const std::filesystem::path& root)
{
    const auto path = root / "apply_reload.md";
    writeText(path, "original");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
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

// ---- Scintilla 后端特有用例 ----

void testExternalBufferChangeBumpsVersion(const std::filesystem::path& root)
{
    const auto path = root / "external_change.md";
    writeText(path, "start");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    const auto v0 = backend.snapshot().version;

    // 模拟 GUI 用户键入：绕过 apply() 直接改缓冲
    fx.editor.send(SCI_APPENDTEXT, 5,
        reinterpret_cast<Scintilla::sptr_t>(const_cast<char*>("-more")));

    QApplication::processEvents();
    const auto snap = backend.snapshot();
    require(snap.version == v0 + 1,
        "buffer change outside apply() must bump version via modified signal");
    require(backend.read({mqt::core::ByteRange{0, 10}}) == "start-more",
        "external buffer change must be visible in read()");
}

void testApplySingleUndoGroup(const std::filesystem::path& root)
{
    const auto path = root / "undo_group.md";
    writeText(path, "hello world");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    // 契约要求编辑按文档顺序（升序）给出
    mqt::core::TextEdit e1;
    e1.start = 0; e1.end = 5; e1.newText = "HELLO";
    mqt::core::TextEdit e2;
    e2.start = 5; e2.end = 5; e2.newText = " there";
    const auto result = backend.apply({e1, e2}, mqt::core::kInitialDocumentVersion);
    require(result.error == mqt::core::ApplyError::None,
        "ascending multi-edit must succeed");

    require(backend.read({mqt::core::ByteRange{0, 17}}) == "HELLO there world",
        "multi-edit content must be applied");
    fx.editor.send(SCI_UNDO);
    const auto undone = backend.read({mqt::core::ByteRange{0, 11}});
    require(undone == "hello world",
        "one apply() must collapse into a single undo group");
}

// ---- M10：后台分块装载 ----

void waitForLoadFinished(mqt::backend::ScintillaDocumentBackend& backend,
    bool* finished, bool* ok)
{
    QElapsedTimer timer;
    while (!*finished && timer.elapsed() < 10000) {
        QApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    Q_UNUSED(ok);
}

void testBackgroundLoadContentAndMetadata(const std::filesystem::path& root)
{
    const auto path = root / "bg-load.md";
    // ~200 KiB，混入 CRLF 与多字节字符，验证分块边界
    std::string body;
    for (int i = 0; i < 20000; ++i) {
        body += (i % 50 == 0) ? std::string("行\xe4\xb8\xad\r\n")
                              : std::string("line of text\n");
    }
    const std::string content = "\xEF\xBB\xBF" + body;
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << content;
    }

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    backend.openSync(path);
    const auto expected = backend.read({mqt::core::ByteRange{0, body.size()}});
    const auto expectedFingerprint = [&] {
        mqt::core::FingerprintSink sink;
        sink.update(body);
        return sink.value();
    }();

    // 后台重载同一文件
    bool finished = false;
    bool ok = false;
    QString loadError;
    std::uint64_t lastLoaded = 0, total = 0;
    QObject::connect(&backend, &mqt::backend::ScintillaDocumentBackend::loadProgress,
        [&](std::uint64_t loaded, std::uint64_t totalBytes) {
            lastLoaded = loaded;
            total = totalBytes;
        });
    QObject::connect(&backend, &mqt::backend::ScintillaDocumentBackend::loadFinished,
        [&](bool success, const QString& error) {
            finished = true;
            ok = success;
            loadError = error;
        });
    require(backend.startBackgroundLoad(path, 16 * 1024),
        "background load must start");
    int eventLoops = 0;
    QElapsedTimer timer;
    timer.start();
    while (!finished && timer.elapsed() < 10000) {
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        ++eventLoops;
    }
    require(finished && ok, ("background load must complete successfully, err=" +
        loadError.toStdString()).c_str());
    // 装载经过排队信号在事件循环中逐块递交；循环至少泵过一次即证明
    // UI 线程未阻塞（大文件的响应性在 300MB 验收环节单独度量）。
    require(eventLoops >= 1, "UI event loop must run during load");
    require(backend.read({mqt::core::ByteRange{0, body.size()}}) == expected,
        "background-loaded buffer must equal sync-loaded content");
    require(backend.fingerprint() == expectedFingerprint,
        "streaming fingerprint must match content fingerprint");
    require(backend.lineIndex().lineCount() > 0, "line index populated");
    require(backend.info().hasUtf8Bom, "BOM metadata preserved");
    require(backend.info().newlineStyleKnown, "newline style known after load");
    require(backend.snapshot().version == mqt::core::kInitialDocumentVersion,
        "background load must not bump version");
    Q_UNUSED(lastLoaded);
    Q_UNUSED(total);
}

void testBackgroundLoadCancel(const std::filesystem::path& root)
{
    const auto path = root / "bg-cancel.md";
    std::string body;
    for (int i = 0; i < 24; ++i) {
        body += std::string(1024 * 1024, 'x');
        body += '\n';
    }
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << body;
    }

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    bool finished = false;
    bool ok = true;
    QObject::connect(&backend, &mqt::backend::ScintillaDocumentBackend::loadFinished,
        [&](bool success, const QString&) {
            finished = true;
            ok = success;
        });
    require(backend.startBackgroundLoad(path, 1024 * 1024),
        "background load must start");
    backend.cancelBackgroundLoad();

    QElapsedTimer timer;
    timer.start();
    while (!finished && timer.elapsed() < 10000) {
        QApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    require(finished, "cancelled load must still deliver loadFinished");
    require(!ok, "cancelled load must report failure");

    // 取消后缓冲必须为空且可立即重新打开（资源确定释放）
    require(backend.info().sizeBytes == 0, "cancelled load must clear the buffer");

    // 重复装载/取消循环稳定性
    for (int i = 0; i < 3; ++i) {
        bool done = false;
        QObject::connect(&backend, &mqt::backend::ScintillaDocumentBackend::loadFinished,
            [&](bool, const QString&) { done = true; });
        require(backend.startBackgroundLoad(path, 1024 * 1024), "restart load");
        backend.cancelBackgroundLoad();
        QElapsedTimer t2;
        while (!done && t2.elapsed() < 10000) {
            QApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        require(done, "repeated cancel must finish promptly");
    }
}

// ---- M11：分批搜索与替换 ----

void testSearchBatchPagination(const std::filesystem::path& root)
{
    const auto path = root / "batch-search.md";
    std::string content;
    const int kNeedles = 25;
    for (int i = 0; i < kNeedles; ++i) {
        content += "padding padding padding padding\n";
        content += "needle here\n";
    }
    writeText(path, content);

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    mqt::backend::ScintillaDocumentBackend::SearchBatchRequest req;
    req.needle = "needle";
    req.maxResults = 10;
    req.maxWindow = 64 * 1024;

    std::vector<mqt::core::SearchHit> all;
    std::uint64_t offset = 0;
    int batches = 0;
    for (;;) {
        req.startOffset = offset;
        const auto r = backend.searchBatch(req);
        ++batches;
        all.insert(all.end(), r.hits.begin(), r.hits.end());
        require(r.hits.size() <= 10, "batch must respect maxResults");
        if (r.cancelled || r.timeout) {
            require(false, "no cancel/timeout expected in pagination test");
        }
        if (r.exhausted) {
            break;
        }
        require(r.nextOffset > offset, "pagination must make progress");
        offset = r.nextOffset;
        require(batches < 100, "pagination must terminate");
    }
    require(all.size() == kNeedles, "all needle hits must be found");
    require(batches == 3, "25 hits with cap 10 must take 3 batches");
    for (std::size_t i = 1; i < all.size(); ++i) {
        require(all[i].sourceRange.start > all[i - 1].sourceRange.start,
            "hits must be ordered and non-overlapping");
    }
    require(all[0].position.line == 2, "first hit on line 2");
}

void testSearchBatchTruncatedFlag(const std::filesystem::path& root)
{
    const auto path = root / "batch-trunc.md";
    std::string content;
    for (int i = 0; i < 50; ++i) {
        content += "needle ";
    }
    writeText(path, content);

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    mqt::backend::ScintillaDocumentBackend::SearchBatchRequest req;
    req.needle = "needle";
    req.maxResults = 5;
    const auto r = backend.searchBatch(req);
    require(r.hits.size() == 5, "batch capped at maxResults");
    require(r.truncated, "truncated flag set when more matches follow");
    require(!r.exhausted, "not exhausted when truncated");
}

void testSearchBatchRegexAndTimeout(const std::filesystem::path& root)
{
    const auto path = root / "batch-regex.md";
    std::string content;
    for (int i = 0; i < 20000; ++i) {
        content += "abcde ";
    }
    writeText(path, content);

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    mqt::backend::ScintillaDocumentBackend::SearchBatchRequest req;
    req.needle = "a[a-z]+d"; // regex
    req.regex = true;
    req.matchCase = true;
    req.maxResults = 100000;
    req.deadlineMs = 1; // force timeout flag quickly
    const auto r = backend.searchBatch(req);
    require(r.hits.size() > 0, "regex search must find matches");
    require(r.timeout || r.exhausted,
        "regex batch must report timeout or completion");
}

void testSearchBatchCancelled(const std::filesystem::path& root)
{
    const auto path = root / "batch-cancel.md";
    writeText(path, "needle needle needle");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    std::atomic_bool cancel{true};
    mqt::backend::ScintillaDocumentBackend::SearchBatchRequest req;
    req.needle = "needle";
    const auto r = backend.searchBatch(req, &cancel);
    require(r.cancelled, "pre-set cancel flag must return cancelled");
    require(r.hits.empty(), "cancelled batch must have no hits");
}

void testReplaceConfirmationGate(const std::filesystem::path& root)
{
    // 33MiB 文档：全量替换受 32MiB 确认门约束
    const auto path = root / "replace-gate.md";
    const std::size_t big = 33ULL << 20;
    std::string content(big, 'a');
    content.replace(content.size() - 2, 2, "zz");
    writeText(path, content);

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    mqt::core::TextEdit edit;
    edit.start = 0;
    edit.end = big;
    edit.newText = "b";
    const std::vector<mqt::core::TextEdit> edits{edit};

    const auto snap = backend.snapshot();
    const auto denied = backend.applyReplace(edits, snap.version, false);
    require(denied.error == mqt::core::ApplyError::ConfirmationRequired,
        ">32MiB replace without confirm must be rejected");
    require(denied.newVersion == snap.version,
        "rejected replace must not change version");

    const auto allowed = backend.applyReplace(edits, snap.version, true);
    require(allowed.error == mqt::core::ApplyError::None,
        "confirmed replace must succeed");
    require(allowed.newVersion == snap.version + 1,
        "confirmed replace must bump version");
    require(backend.read({mqt::core::ByteRange{0, 1}}) == "b",
        "confirmed replace must apply the edit");
    require(backend.info().sizeBytes == 1, "replaced content must be 1 byte");
}

void testSmallReplaceNeedsNoConfirmation(const std::filesystem::path& root)
{
    const auto path = root / "replace-small.md";
    writeText(path, "hello world");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    mqt::core::TextEdit edit;
    edit.start = 6; edit.end = 11; edit.newText = "there";
    const auto r = backend.applyReplace({edit}, mqt::core::kInitialDocumentVersion, false);
    require(r.error == mqt::core::ApplyError::None,
        "small replace must not require confirmation");
    require(backend.read({mqt::core::ByteRange{0, 11}}) == "hello there",
        "small replace must apply");
}

// ---- M12：恢复点 / 外部指纹复核 / 撤销预算 ----

std::string readWhole(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
}

void testSavePointDirtyTracking(const std::filesystem::path& root)
{
    const auto path = root / "savepoint.md";
    writeText(path, "clean");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    require(!backend.isDirty(), "freshly loaded document must be clean");

    mqt::core::TextEdit edit;
    edit.start = 5; edit.end = 5; edit.newText = " edit";
    const auto r = backend.apply({edit}, mqt::core::kInitialDocumentVersion);
    require(r.error == mqt::core::ApplyError::None, "edit must apply");
    require(backend.isDirty(), "edited document must be dirty");
}

void testSavePointAfterSave(const std::filesystem::path& root)
{
    const auto path = root / "savepoint2.md";
    writeText(path, "clean");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    mqt::core::TextEdit edit;
    edit.start = 5; edit.end = 5; edit.newText = " edit";
    (void)backend.apply({edit}, mqt::core::kInitialDocumentVersion);
    require(backend.isDirty(), "edited document must be dirty");

    backend.save(nullptr);
    require(!backend.isDirty(), "save must clear the dirty flag");

    const auto versionBefore = backend.snapshot().version;
    const auto snap = backend.reload();
    require(!backend.isDirty(), "reload must be clean");
    require(snap.version == versionBefore + 1, "reload bumps version");
}

void testExternalFingerprintReview(const std::filesystem::path& root)
{
    const auto path = root / "external-review.md";
    writeText(path, "original content");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    require(!backend.isDirty(), "fresh load must be clean");

    // 外部改盘（模拟另一个进程/编辑器写入）
    writeText(path, "EXTERNALLY MODIFIED CONTENT");

    bool threw = false;
    std::string errorText;
    try {
        backend.save(nullptr);
    } catch (const std::runtime_error& e) {
        threw = true;
        errorText = e.what();
    }
    require(threw, "save over external modification must be rejected");
    require(errorText.find("changed on disk") != std::string::npos,
        "rejection must explain the external modification");
    require(readWhole(path) == "EXTERNALLY MODIFIED CONTENT",
        "rejected save must not touch the externally modified file");

    // 另存为新目标不受复核限制（内存内容仍为装载时的 original content）
    const auto other = root / "external-review-out.md";
    backend.saveAs(other);
    require(readWhole(other) == "original content",
        "saveAs must write the in-memory content");

    // 重新装载外部修改后的磁盘内容，基线重建
    backend.openSync(path);
    require(!backend.isDirty(), "reload must be clean");
    require(backend.read({mqt::core::ByteRange{0, 27}}) ==
            "EXTERNALLY MODIFIED CONTENT",
        "reload must pick up external changes");

    // 基线重建后编辑与保存恢复正常
    mqt::core::TextEdit edit;
    edit.start = 27; edit.end = 27; edit.newText = "!";
    const auto r = backend.apply({edit}, backend.snapshot().version);
    require(r.error == mqt::core::ApplyError::None, "edit must apply");
    backend.save(nullptr);
    require(readWhole(path) == "EXTERNALLY MODIFIED CONTENT!",
        "post-reload save must persist the edit");
}

void testUndoBudget(const std::filesystem::path& root)
{
    const auto path = root / "undo-budget.md";
    writeText(path, "seed");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    backend.setUndoBudgetBytes(16); // 极小预算便于测试

    mqt::core::TextEdit edit;
    edit.start = 4; edit.end = 4; edit.newText = std::string(64, 'x');
    (void)backend.apply({edit}, mqt::core::kInitialDocumentVersion);
    require(!backend.canUndo(), "budget exceeded must clear undo history");
    require(backend.read({mqt::core::ByteRange{0, 68}}) ==
            std::string("seed") + std::string(64, 'x'),
        "undo budget clear must not alter content");

    // 后续编辑仍正常
    mqt::core::TextEdit edit2;
    edit2.start = 0; edit2.end = 4; edit2.newText = "SEED";
    const auto r = backend.apply({edit2}, backend.snapshot().version);
    require(r.error == mqt::core::ApplyError::None, "edits keep working after budget clear");
}

void testApplyEmptyEditsKeepsVersion(const std::filesystem::path& root)
{
    const auto path = root / "apply_empty.md";
    writeText(path, "data");

    Fixture fx;
    mqt::backend::ScintillaDocumentBackend backend(fx.editor, path);
    const auto result = backend.apply({}, mqt::core::kInitialDocumentVersion);
    require(result.error == mqt::core::ApplyError::None,
        "empty edit list must succeed");
    require(result.newVersion == mqt::core::kInitialDocumentVersion,
        "empty edit list must not bump version");
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("markdown_qt_scintilla_backend_tests");
    const struct {
        const char* name;
        void (*fn)(const std::filesystem::path&);
    } tests[] = {
        {"OpenSnapshot", testOpenSnapshot},
        {"OpenSnapshotWithBom", testOpenSnapshotWithBom},
        {"ReadCorrectness", testReadCorrectness},
        {"ReadOutOfRange", testReadOutOfRange},
        {"LocateLinesBasic", testLocateLinesBasic},
        {"LocateLinesWithBom", testLocateLinesWithBom},
        {"LocateLinesOutOfRange", testLocateLinesOutOfRange},
        {"SearchMatch", testSearchMatch},
        {"SearchTruncated", testSearchTruncated},
        {"SearchCancelled", testSearchCancelled},
        {"ApplySingleEdit", testApplySingleEdit},
        {"ApplyMultipleEdits", testApplyMultipleEdits},
        {"ApplyStaleVersion", testApplyStaleVersion},
        {"ApplyOverlappingEdits", testApplyOverlappingEdits},
        {"ApplyRangeInvalid", testApplyRangeInvalid},
        {"SaveRoundTrip", testSaveRoundTrip},
        {"SaveReplacesContent", testSaveReplacesContent},
        {"SaveToReadOnlyDir", testSaveToReadOnlyDir},
        {"Reload", testReload},
        {"ReloadNoChange", testReloadNoChange},
        {"ApplyEditsAfterReload", testApplyEditsAfterReload},
        {"ExternalBufferChangeBumpsVersion", testExternalBufferChangeBumpsVersion},
        {"ApplySingleUndoGroup", testApplySingleUndoGroup},
        {"ApplyEmptyEditsKeepsVersion", testApplyEmptyEditsKeepsVersion},
        {"BackgroundLoadContentAndMetadata", testBackgroundLoadContentAndMetadata},
        {"BackgroundLoadCancel", testBackgroundLoadCancel},
        {"SearchBatchPagination", testSearchBatchPagination},
        {"SearchBatchTruncatedFlag", testSearchBatchTruncatedFlag},
        {"SearchBatchRegexAndTimeout", testSearchBatchRegexAndTimeout},
        {"SearchBatchCancelled", testSearchBatchCancelled},
        {"ReplaceConfirmationGate", testReplaceConfirmationGate},
        {"SmallReplaceNeedsNoConfirmation", testSmallReplaceNeedsNoConfirmation},
        {"SavePointDirtyTracking", testSavePointDirtyTracking},
        {"SavePointAfterSave", testSavePointAfterSave},
        {"ExternalFingerprintReview", testExternalFingerprintReview},
        {"UndoBudget", testUndoBudget},
    };
    try {
        const auto root = testRoot();
        for (const auto& t : tests) {
            std::cerr << "[case] " << t.name << "\n";
            t.fn(root);
        }
        std::filesystem::remove_all(root);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
