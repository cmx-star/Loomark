#pragma once

#include "backend/scintilla_document_backend.h"

#include <QObject>
#include <QPointer>
#include <filesystem>

class ScintillaEditBase;

namespace mqt::gui {

/// One open document: its own Scintilla editor, its own backend, its own
/// metadata and cancellation domain. Ownership structure makes isolation
/// automatic — a destroyed session cancels its in-flight load and disconnects
/// every signal, so late chunks can never reach another session's editor.
class DocumentSession final : public QObject {
    Q_OBJECT
public:
    explicit DocumentSession(std::uint64_t generation, QObject* parent = nullptr);
    ~DocumentSession() override;

    DocumentSession(const DocumentSession&) = delete;
    DocumentSession& operator=(const DocumentSession&) = delete;

    [[nodiscard]] std::uint64_t generation() const { return generation_; }
    [[nodiscard]] ScintillaEditBase& editor() { return *editor_; }
    [[nodiscard]] mqt::backend::ScintillaDocumentBackend& backend() { return *backend_; }
    [[nodiscard]] bool hasBackend() const { return backend_ != nullptr; }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    bool openSync(const std::filesystem::path& path);
    bool openBackground(const std::filesystem::path& path, std::uint64_t blockSize = 0);
    bool save();
    bool saveAs(const std::filesystem::path& path);
    void cancelLoad();

    [[nodiscard]] bool isLoadInProgress() const
    {
        return backend_ != nullptr && backend_->isLoadInProgress();
    }
    [[nodiscard]] bool isDirty() const
    {
        return backend_ != nullptr && backend_->isDirty();
    }

signals:
    void loadProgress(std::uint64_t loadedBytes, std::uint64_t totalBytes);
    void loadFinished(bool ok, const QString& error);
    void contentsChanged();
    void undoBudgetExceeded();

private:
    void ensureBackend();

    std::uint64_t generation_;
    // QPointer guards against the editor being destroyed by the widget tree
    // before the session's own destructor runs (MainWindow teardown order).
    QPointer<ScintillaEditBase> editorGuard_;
    ScintillaEditBase* editor_ = nullptr;
    mqt::backend::ScintillaDocumentBackend* backend_ = nullptr;
    std::filesystem::path path_;
};

} // namespace mqt::gui
