#include "gui/preview_index_worker.h"

namespace mqt::gui {

PreviewIndexThread::PreviewIndexThread(std::filesystem::path path,
    mqt::core::BuildPreviewOptions options,
    std::uint64_t generation,
    QObject* parent)
    : QThread(parent)
    , path_(std::move(path))
    , options_(std::move(options))
    , generation_(generation)
{
}

PreviewIndexThread::~PreviewIndexThread()
{
    cancel();
    wait();
}

void PreviewIndexThread::cancel()
{
    cancelled_.store(true, std::memory_order_relaxed);
}

void PreviewIndexThread::run()
{
    try {
        options_.cancelFlag = &cancelled_;
        index_ = mqt::core::buildPreviewIndex(path_, options_);
        if (index_.cancelled || cancelled_.load(std::memory_order_relaxed)) {
            errorMessage_ = QStringLiteral("preview indexing cancelled");
            success_ = false;
            return;
        }
        success_ = true;
    } catch (const std::exception& error) {
        errorMessage_ = QString::fromUtf8(error.what());
        success_ = false;
    } catch (...) {
        errorMessage_ = QStringLiteral("unknown preview indexing error");
        success_ = false;
    }
}

} // namespace mqt::gui
