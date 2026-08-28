#pragma once

#include "core/load_scanner.h"

#include <QByteArray>
#include <QObject>
#include <QThread>

#include <atomic>
#include <filesystem>
#include <string>

namespace mqt::backend {

/// Background chunked file loader for the Scintilla backend. The worker
/// thread reads the file in blocks and feeds the core load scanners; each
/// block is queued to the UI thread (which appends it into the Scintilla
/// buffer). Flow control uses an atomic in-flight counter with a polling
/// wait in the worker — deliberately free of blocking primitives on the
/// UI side, so a stalled consumer can never deadlock the loader.
///
/// Cancellation: cancelFlag_ is polled between blocks; the GUI/backend discards
/// whatever already arrived. The destructor blocks until the thread is done.
class ScintillaLoadTask final : public QThread {
    Q_OBJECT

public:
    static constexpr std::uint64_t kDefaultBlockSize = 4ULL << 20; // 4 MiB
    static constexpr int kMaxInFlight = 2; // bounded pending-chunk memory

    ScintillaLoadTask(std::filesystem::path path, std::uint64_t blockSize,
        QObject* parent = nullptr);
    ~ScintillaLoadTask() override;

    ScintillaLoadTask(const ScintillaLoadTask&) = delete;
    ScintillaLoadTask& operator=(const ScintillaLoadTask&) = delete;

    void cancel() { cancelFlag_.store(true, std::memory_order_relaxed); }
    /// Called by the UI thread after a chunkReady block was appended.
    void chunkConsumed() { inFlight_.fetch_sub(1, std::memory_order_relaxed); }

    [[nodiscard]] const mqt::core::SparseLineIndex& lineIndex() const { return lineIndex_.index(); }
    [[nodiscard]] std::uint64_t fingerprint() const { return fingerprint_.value(); }
    [[nodiscard]] mqt::core::NewlineCounts newlineCounts() const { return newlineCounter_.counts(); }
    [[nodiscard]] bool cancelled() const { return cancelFlag_.load(std::memory_order_relaxed); }

signals:
    /// Delivered on the UI thread (queued connection).
    void chunkReady(const QByteArray& bytes);
    void progress(std::uint64_t loadedBytes, std::uint64_t totalBytes);
    void loadFinished(bool ok, const QString& error);

protected:
    void run() override;

private:
    std::filesystem::path path_;
    std::uint64_t blockSize_;
    std::atomic_bool cancelFlag_{false};
    std::atomic_int inFlight_{0}; // chunks emitted but not yet appended

    mqt::core::LineIndexBuilder lineIndex_;
    mqt::core::FingerprintSink fingerprint_;
    mqt::core::NewlineCounter newlineCounter_;
};

} // namespace mqt::backend
