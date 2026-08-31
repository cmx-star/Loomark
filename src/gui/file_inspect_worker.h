#pragma once

#include "core/document_file.h"

#include <QThread>
#include <QString>

#include <atomic>
#include <filesystem>

namespace mqt::gui {

/// Runs mqt::core::inspectFile (full newline-style scan) on a background
/// thread so the UI thread never blocks on a whole-file read.
///
/// The thread never touches widgets; the owner receives results through the
/// QThread::finished signal on the UI thread. Each instance runs exactly one
/// job: create, start(), then deleteLater() from the finished handler.
/// cancel() asks run() to stop early; the destructor cancels and waits, so
/// destroying the window while a scan runs is always safe.
class FileInspectThread final : public QThread {
    Q_OBJECT

public:
    explicit FileInspectThread(std::filesystem::path path,
        std::uint64_t generation,
        QObject* parent = nullptr);
    ~FileInspectThread() override;

    void cancel();
    [[nodiscard]] bool cancelled() const { return cancelled_; }
    [[nodiscard]] std::uint64_t generation() const { return generation_; }
    [[nodiscard]] bool success() const { return success_; }
    [[nodiscard]] const QString& errorMessage() const { return errorMessage_; }
    [[nodiscard]] const mqt::core::FileInfo& info() const { return info_; }

protected:
    void run() override;

private:
    std::filesystem::path path_;
    std::uint64_t generation_ = 0;
    std::atomic_bool cancelled_{false};
    bool success_ = false;
    QString errorMessage_;
    mqt::core::FileInfo info_;
};

} // namespace mqt::gui
