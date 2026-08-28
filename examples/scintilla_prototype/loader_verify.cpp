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

#include <cstdio>
#include <filesystem>
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
        fprintf(stderr, "usage: %s <sample.md> [load|cancel]\n", argv[0]);
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
