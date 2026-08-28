#include "backend/scintilla_load_task.h"

#include <fstream>
#include <system_error>

namespace mqt::backend {

ScintillaLoadTask::ScintillaLoadTask(std::filesystem::path path, std::uint64_t blockSize,
    QObject* parent)
    : QThread(parent)
    , path_(std::move(path))
    , blockSize_(blockSize == 0 ? kDefaultBlockSize : blockSize)
{
}

ScintillaLoadTask::~ScintillaLoadTask()
{
    cancelFlag_.store(true, std::memory_order_relaxed);
    if (isRunning()) {
        wait();
    }
}

void ScintillaLoadTask::run()
{
    std::error_code ec;
    const auto total = std::filesystem::file_size(path_, ec);
    if (ec) {
        emit loadFinished(false, QStringLiteral("failed to read file size"));
        return;
    }

    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        emit loadFinished(false, QStringLiteral("failed to open file"));
        return;
    }

    std::uint64_t rawLoaded = 0;     // bytes consumed from the file (incl. BOM)
    std::uint64_t contentLoaded = 0; // bytes fed to scanners / buffer
    bool cancelled = false;
    bool first = true;
    QString error;
    std::string chunk;
    chunk.resize(blockSize_);

    while (rawLoaded < total) {
        // Bounded backlog without a blocking primitive: poll until the UI
        // thread drained enough chunks. Every wait re-checks the cancel flag,
        // so this can never deadlock against a stalled consumer.
        while (inFlight_.load(std::memory_order_relaxed) >= kMaxInFlight &&
            !cancelFlag_.load(std::memory_order_relaxed)) {
            QThread::msleep(1);
        }
        if (cancelFlag_.load(std::memory_order_relaxed)) {
            cancelled = true;
            break;
        }

        input.read(chunk.data(), static_cast<std::streamsize>(
            std::min<std::uint64_t>(blockSize_, total - rawLoaded)));
        auto got = static_cast<std::size_t>(input.gcount());
        if (got == 0) {
            if (input.eof()) {
                break; // clean EOF
            }
            error = QStringLiteral("read error");
            break;
        }

        // The Scintilla buffer holds content without the BOM (M09
        // semantics); strip it from the first chunk if present.
        std::string_view data(chunk.data(), got);
        if (first) {
            first = false;
            if (got >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
                static_cast<unsigned char>(data[1]) == 0xBB &&
                static_cast<unsigned char>(data[2]) == 0xBF) {
                data.remove_prefix(3);
            }
        }
        rawLoaded += got;

        if (!data.empty()) {
            lineIndex_.feed(data, contentLoaded);
            fingerprint_.update(data);
            newlineCounter_.feed(data);
            contentLoaded += data.size();
            emit progress(rawLoaded, total);
            inFlight_.fetch_add(1, std::memory_order_relaxed);
            emit chunkReady(QByteArray(data.data(), static_cast<qsizetype>(data.size())));
        }
    }

    if (total == 0) {
        emit progress(0, 0);
    }

    newlineCounter_.finish();
    lineIndex_.finish(contentLoaded);
    emit loadFinished(!cancelled && error.isEmpty(), cancelled
        ? QStringLiteral("cancelled")
        : error);
}

} // namespace mqt::backend
