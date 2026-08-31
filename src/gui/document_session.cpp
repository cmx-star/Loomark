#include "gui/document_session.h"

#include <QFrame>
#include <ScintillaEditBase.h>

namespace mqt::gui {

DocumentSession::DocumentSession(std::uint64_t generation, QObject* parent)
    : QObject(parent)
    , generation_(generation)
{
    editor_ = new ScintillaEditBase();
    editorGuard_ = editor_;
    editor_->setFrameShape(QFrame::NoFrame);
    editor_->send(SCI_SETCODEPAGE, SC_CP_UTF8);
    editor_->send(SCI_SETTABWIDTH, 4);
    editor_->send(SCI_SETMARGINWIDTHN, 1, 0);
    editor_->send(SCI_SETWRAPMODE, SC_WRAP_WORD);
    editor_->send(SCI_SETUNDOCOLLECTION, 1);
}

DocumentSession::~DocumentSession()
{
    // Deleting the editor destroys the backend (its QObject parent) and, in
    // turn, cancels and joins any in-flight load task. The editor removes
    // itself from the editor stack on destruction. If the widget tree
    // already deleted the editor (teardown-order race), the QPointer is
    // cleared and this becomes a no-op.
    if (!editorGuard_.isNull()) {
        editorGuard_->setParent(nullptr);
        delete editor_;
    }
    editor_ = nullptr;
    backend_ = nullptr;
}

bool DocumentSession::openSync(const std::filesystem::path& path)
{
    ensureBackend();
    backend_->openSync(path);
    path_ = path;
    return true;
}

bool DocumentSession::openBackground(const std::filesystem::path& path,
    std::uint64_t blockSize)
{
    ensureBackend();
    const bool started = backend_->startBackgroundLoad(path, blockSize);
    if (started) {
        path_ = path;
    }
    return started;
}

bool DocumentSession::save()
{
    if (backend_ == nullptr) {
        return false;
    }
    backend_->save();
    return true;
}

bool DocumentSession::saveAs(const std::filesystem::path& path)
{
    if (backend_ == nullptr) {
        return false;
    }
    backend_->saveAs(path);
    path_ = path;
    return true;
}

void DocumentSession::cancelLoad()
{
    if (backend_ != nullptr) {
        backend_->cancelBackgroundLoad();
    }
}

void DocumentSession::ensureBackend()
{
    if (backend_ != nullptr) {
        return;
    }
    backend_ = new mqt::backend::ScintillaDocumentBackend(*editor_);

    connect(backend_, &mqt::backend::ScintillaDocumentBackend::loadProgress,
        this, &DocumentSession::loadProgress);
    connect(backend_, &mqt::backend::ScintillaDocumentBackend::loadFinished,
        this, &DocumentSession::loadFinished);
    connect(backend_, &mqt::backend::ScintillaDocumentBackend::undoBudgetExceeded,
        this, &DocumentSession::undoBudgetExceeded);
    connect(backend_, &mqt::backend::ScintillaDocumentBackend::contentsChanged,
        this, &DocumentSession::contentsChanged);
}

} // namespace mqt::gui
