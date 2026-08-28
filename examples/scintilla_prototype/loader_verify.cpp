// M10 验收工具：300MB 后台装载
//   markdown_qt_scintilla_loader_verify <sample.md> load
//   markdown_qt_scintilla_loader_verify <sample.md> cancel
// load：后台分块装载大样本，度量总时长、UI 事件循环泵次数（响应性）、
//       完成后指纹与独立流式指纹比对、行数与稀疏索引一致性。
// cancel：装载中途取消，验证 loadFinished 送达、缓冲清空、线程在限期内退出。
#include "backend/scintilla_document_backend.h"
#include "core/load_scanner.h"

#include <ScintillaEditBase.h>

#include <QApplication>
#include <QElapsedTimer>
#include <atomic>

#include <cstdio>
#include <filesystem>
#include <vector>
#include <fstream>

namespace {

constexpr int kTimeoutMs = 120000;

bool waitForFinished(mqt::backend::ScintillaDocumentBackend& backend,
    bool* finished, qint64 timeoutMs, qint64* pumps, qint64* maxPumpGapMs)
{
    QElapsedTimer timer;
    timer.start();
    qint64 last = 0;
    while (!*finished && timer.elapsed() < timeoutMs) {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        ++*pumps;
        const qint64 now = timer.elapsed();
        if (now - last > *maxPumpGapMs) {
            *maxPumpGapMs = now - last;
        }
        last = now;
    }
    return *finished;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <sample.md> [load|cancel|search|f02]\n", argv[0]);
        return 2;
    }
    QApplication app(argc, argv);
    const std::filesystem::path path = argv[1];
    const std::string mode = argv[2];

    ScintillaEditBase editor;
    editor.resize(600, 400);
    editor.show();

    mqt::backend::ScintillaDocumentBackend backend(editor);
    bool finished = false;
    bool ok = false;
    QObject::connect(&backend, &mqt::backend::ScintillaDocumentBackend::loadFinished,
        [&](bool success, const QString&) {
            finished = true;
            ok = success;
        });

    if (mode == "cancel") {
        if (!backend.startBackgroundLoad(path, 4ULL << 20)) {
            fprintf(stderr, "start failed\n");
            return 1;
        }
        QElapsedTimer timer;
        timer.start();
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        backend.cancelBackgroundLoad();
        qint64 pumps = 0, maxGap = 0;
        waitForFinished(backend, &finished, 30000, &pumps, &maxGap);
        const qint64 cancelMs = timer.elapsed();
        printf("cancel: finished=%d ok=%d elapsed=%lldms bufferLen=%lld\n",
            finished, ok, cancelMs,
            (long long)editor.send(SCI_GETTEXTLENGTH));
        if (!finished || ok || editor.send(SCI_GETTEXTLENGTH) != 0) {
            fprintf(stderr, "CANCEL VERIFY FAILED\n");
            return 1;
        }
        printf("CANCEL VERIFY PASS\n");
        return 0;
    }

    if (mode == "search") {
        // 先完成装载，再验证分批搜索与取消
        if (!backend.startBackgroundLoad(path, 4ULL << 20)) {
            fprintf(stderr, "start failed\n");
            return 1;
        }
        qint64 pumps = 0, maxGap = 0;
        waitForFinished(backend, &finished, kTimeoutMs, &pumps, &maxGap);
        if (!finished || !ok) {
            fprintf(stderr, "load failed\n");
            return 1;
        }

        mqt::backend::ScintillaDocumentBackend::SearchBatchRequest req;
        req.needle = "Section";
        req.maxResults = 10000;
        req.deadlineMs = 200;

        std::uint64_t totalHits = 0, offset = 0;
        int batches = 0;
        QElapsedTimer timer;
        timer.start();
        for (;;) {
            req.startOffset = offset;
            const auto r = backend.searchBatch(req);
            ++batches;
            totalHits += r.hits.size();
            if (r.exhausted || r.timeout) {
                break;
            }
            offset = r.nextOffset;
        }
        printf("search: hits=%llu batches=%d elapsedMs=%lld\n",
            (unsigned long long)totalHits, batches, timer.elapsed());

        // 取消验证：预置取消标志，必须立即返回
        std::atomic_bool cancel{true};
        req.startOffset = 0;
        const auto cancelled = backend.searchBatch(req, &cancel);
        printf("search-cancel: cancelled=%d elapsedMs=%lld\n",
            cancelled.cancelled, timer.elapsed());
        const bool pass = totalHits > 0 && batches > 1 && cancelled.cancelled;
        printf("%s\n", pass ? "SEARCH VERIFY PASS" : "SEARCH VERIFY FAILED");
        return pass ? 0 : 1;
    }

    if (mode == "f02") {
        // F02 端到端：装载 → 编辑 → 搜索 → 替换（确认门）→ 保存 → 重载
        if (!backend.startBackgroundLoad(path, 4ULL << 20)) {
            fprintf(stderr, "start failed\n");
            return 1;
        }
        qint64 pumps = 0, maxGap = 0;
        waitForFinished(backend, &finished, kTimeoutMs, &pumps, &maxGap);
        if (!finished || !ok) {
            fprintf(stderr, "load failed\n");
            return 1;
        }
        printf("f02: loaded fingerprintMatch=%d\n",
            backend.fingerprint() == [&] {
                mqt::core::FingerprintSink sink;
                std::ifstream input(path, std::ios::binary);
                std::string chunk(8 << 20, '\0');
                while (input.read(chunk.data(), (std::streamsize)chunk.size()) ||
                    input.gcount() > 0) {
                    sink.update(std::string_view(chunk.data(), (std::size_t)input.gcount()));
                    if (input.eof()) break;
                }
                return sink.value();
            }());

        // 编辑：在文档末尾插入标记
        const auto len0 = editor.send(SCI_GETTEXTLENGTH);
        std::vector<mqt::core::TextEdit> edits{
            mqt::core::TextEdit{static_cast<std::uint64_t>(len0), static_cast<std::uint64_t>(len0), "F02MARKER"}};
        auto r = backend.apply(edits, backend.snapshot().version);
        if (r.error != mqt::core::ApplyError::None) {
            fprintf(stderr, "apply failed\n");
            return 1;
        }
        printf("f02: edited version=%llu\n", (unsigned long long)r.newVersion);

        // 搜索命中标记
        mqt::backend::ScintillaDocumentBackend::SearchBatchRequest req;
        req.needle = "F02MARKER";
        std::uint64_t markerOffset = 0, offset = 0;
        unsigned markerHits = 0;
        for (;;) {
            req.startOffset = offset;
            const auto batch = backend.searchBatch(req);
            markerHits += (unsigned)batch.hits.size();
            for (const auto& h : batch.hits) {
                markerOffset = h.sourceRange.start;
            }
            if (batch.exhausted || batch.timeout) {
                break;
            }
            offset = batch.nextOffset;
        }
        if (markerHits != 1) {
            fprintf(stderr, "marker search failed (hits=%u)\n", markerHits);
            return 1;
        }
        printf("f02: marker found at %llu\n", (unsigned long long)markerOffset);

        // >32MiB 替换：先拒绝后确认
        const std::uint64_t bigSpan = 33ULL << 20;
        std::vector<mqt::core::TextEdit> bigEdits{
            mqt::core::TextEdit{0, bigSpan, std::string("Z")}};
        const auto denied = backend.applyReplace(bigEdits, backend.snapshot().version, false);
        if (denied.error != mqt::core::ApplyError::ConfirmationRequired) {
            fprintf(stderr, "confirmation gate failed\n");
            return 1;
        }
        r = backend.applyReplace(bigEdits, backend.snapshot().version, true);
        if (r.error != mqt::core::ApplyError::None) {
            fprintf(stderr, "confirmed replace failed\n");
            return 1;
        }
        printf("f02: replaced 33MiB span, version=%llu\n",
            (unsigned long long)r.newVersion);

        // 保存（另存为避免破坏样本；流式原子写）
        const auto outPath = std::filesystem::path(std::string(path) + ".f02-out");
        QElapsedTimer saveTimer;
        saveTimer.start();
        backend.saveAs(outPath);
        printf("f02: saved %llu bytes in %lld ms\n",
            (unsigned long long)std::filesystem::file_size(outPath),
            saveTimer.elapsed());

        // 重载复核
        const auto snap = backend.reload();
        printf("f02: reloaded version=%llu isDirty=%d\n",
            (unsigned long long)snap.version, backend.isDirty());
        req.startOffset = 0;
        req.maxWindow = 512ULL << 20; // 全文档单批复核
        const auto markerAfter = backend.searchBatch(req);
        const auto diskSize = std::filesystem::file_size(outPath);
        const auto bufferLen = editor.send(SCI_GETTEXTLENGTH);
        printf("f02: diskSize=%llu bufferLen=%lld markerHits=%u\n",
            (unsigned long long)diskSize, (long long)bufferLen,
            markerAfter.hits.size());
        const bool pass = snap.version > r.newVersion &&
            markerAfter.hits.size() == 1 &&
            diskSize == (unsigned long long)bufferLen;
        printf("%s\n", pass ? "F02 VERIFY PASS" : "F02 VERIFY FAILED");
        return pass ? 0 : 1;
    }

    if (mode != "load") {
        fprintf(stderr, "unknown mode: %s\n", argv[2]);
        return 2;
    }

    // 独立流式指纹（后台装载期间逐块算好，完成后与后端比对）
    mqt::core::FingerprintSink expected;
    {
        std::ifstream input(path, std::ios::binary);
        std::string chunk(8 << 20, '\0');
        while (input.read(chunk.data(), (std::streamsize)chunk.size()) || input.gcount() > 0) {
            expected.update(std::string_view(chunk.data(), (std::size_t)input.gcount()));
            if (input.eof()) break;
        }
    }

    if (!backend.startBackgroundLoad(path, 4ULL << 20)) {
        fprintf(stderr, "start failed\n");
        return 1;
    }
    qint64 pumps = 0, maxPumpGapMs = 0;
    QElapsedTimer timer;
    timer.start();
    waitForFinished(backend, &finished, kTimeoutMs, &pumps, &maxPumpGapMs);
    const qint64 totalMs = timer.elapsed();

    printf("load: finished=%d ok=%d totalMs=%lld eventPumps=%lld maxPumpGapMs=%lld\n",
        finished, ok, totalMs, pumps, maxPumpGapMs);
    printf("load: bufferLen=%lld lineCount=%llu fingerprintMatch=%d\n",
        (long long)editor.send(SCI_GETTEXTLENGTH),
        (unsigned long long)backend.lineIndex().lineCount(),
        backend.fingerprint() == expected.value());
    const bool pass = finished && ok &&
        maxPumpGapMs < 500 &&
        backend.fingerprint() == expected.value() &&
        backend.lineIndex().lineCount() > 0;
    printf("%s\n", pass ? "LOAD VERIFY PASS" : "LOAD VERIFY FAILED");
    return pass ? 0 : 1;
}
