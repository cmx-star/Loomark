#pragma once

#include "core/markdown_index.h"

#include <QThread>
#include <QString>

#include <atomic>
#include <filesystem>

namespace mqt::gui {

/// Runs mqt::core::buildPreviewIndex on a background thread.
///
/// The thread never touches widgets; the owner receives results through the
/// QThread::finished signal on the UI thread and reads them via index().
/// Each instance runs exactly one job: create, start(), then deleteLater()
/// from the finished handler. cancel() cooperatively stops the scan; the
/// destructor cancels and waits, so destroying the owner while a scan runs
/// is always safe.
class PreviewIndexThread final : public QThread {
    Q_OBJECT

public:
    explicit PreviewIndexThread(std::filesystem::path path,
        mqt::core::BuildPreviewOptions options,
        std::uint64_t generation,
        QObject* parent = nullptr);
    ~PreviewIndexThread() override;

    void cancel();
    [[nodiscard]] bool cancelled() const { return cancelled_; }
    [[nodiscard]] std::uint64_t generation() const { return generation_; }
    [[nodiscard]] bool success() const { return success_; }
    [[nodiscard]] const QString& errorMessage() const { return errorMessage_; }
    [[nodiscard]] const mqt::core::PreviewIndex& index() const { return index_; }

protected:
    void run() override;

private:
    std::filesystem::path path_;
    mqt::core::BuildPreviewOptions options_;
    std::uint64_t generation_ = 0;
    std::atomic_bool cancelled_{false};
    bool success_ = false;
    QString errorMessage_;
    mqt::core::PreviewIndex index_;
};

} // namespace mqt::gui
