#include "gui/file_inspect_worker.h"

namespace mqt::gui {

FileInspectThread::FileInspectThread(std::filesystem::path path,
    std::uint64_t generation,
    QObject* parent)
    : QThread(parent)
    , path_(std::move(path))
    , generation_(generation)
{
}

FileInspectThread::~FileInspectThread()
{
    cancel();
    wait();
}

void FileInspectThread::cancel()
{
    cancelled_.store(true, std::memory_order_relaxed);
}

void FileInspectThread::run()
{
    try {
        info_ = mqt::core::inspectFile(path_, &cancelled_);
        success_ = !cancelled_.load(std::memory_order_relaxed) && info_.newlineStyleKnown;
        if (!success_) {
            errorMessage_ = QStringLiteral("inspection cancelled");
        }
    } catch (const std::exception& error) {
        errorMessage_ = QString::fromUtf8(error.what());
        success_ = false;
    } catch (...) {
        errorMessage_ = QStringLiteral("unknown file inspection error");
        success_ = false;
    }
}

} // namespace mqt::gui
