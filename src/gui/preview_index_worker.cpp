#include "gui/preview_index_worker.h"

namespace mqt::gui {

PreviewIndexThread::PreviewIndexThread(std::filesystem::path path,
    mqt::core::BuildPreviewOptions options,
    std::uint64_t generation,
    QObject* parent)
    : QThread(parent)
    , path_(std::move(path))
    , options_(options)
    , generation_(generation)
{
}

void PreviewIndexThread::run()
{
    try {
        index_ = mqt::core::buildPreviewIndex(path_, options_);
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
