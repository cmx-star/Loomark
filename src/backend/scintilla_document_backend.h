#pragma once

#include "core/document_backend.h"

#include <ScintillaEditBase.h>

#include <QObject>
#include <filesystem>

namespace mqt::backend {

/// IDocumentBackend implementation whose single source of truth is the byte
/// buffer of a live ScintillaEditBase widget. The widget itself is NOT owned:
/// production wires the visible editor in, tests create their own instance.
/// All position arithmetic is UTF-8 byte offsets, matching both the M05
/// contract and Scintilla's SC_CP_UTF8 document addressing.
///
/// The backend must live on the UI thread (Scintilla message restriction).
class ScintillaDocumentBackend final : public QObject, public mqt::core::IDocumentBackend {
    Q_OBJECT
public:
    /// Attach to an editor and load `path` into it. Throws for non-Normal
    /// tier files (M09 scope; M10 replaces this with the chunked loader).
    ScintillaDocumentBackend(ScintillaEditBase& editor,
        const std::filesystem::path& path);
    ~ScintillaDocumentBackend() override;

    ScintillaDocumentBackend(const ScintillaDocumentBackend&) = delete;
    ScintillaDocumentBackend& operator=(const ScintillaDocumentBackend&) = delete;

    mqt::core::DocumentSnapshot snapshot() const override;
    mqt::core::DocumentInfo info() const override;
    std::string read(mqt::core::ByteRange range) const override;
    mqt::core::LocateResult locateLines(mqt::core::ByteRange range) const override;
    mqt::core::SearchOutcome search(const mqt::core::SearchQuery& query,
        const std::atomic_bool* cancelFlag = nullptr) const override;
    mqt::core::ApplyResult apply(std::vector<mqt::core::TextEdit> edits,
        mqt::core::DocumentVersion baseVersion,
        const std::atomic_bool* cancelFlag = nullptr) override;
    void save(const std::atomic_bool* cancelFlag = nullptr) override;
    void saveAs(const std::filesystem::path& path) override;
    mqt::core::DocumentSnapshot reload() override;

private slots:
    /// Any buffer change that did not go through apply()/load bumps the
    /// version (user typing, undo, paste, cut, ...). Scintilla emits several
    /// notifications per operation (InsertCheck / BeforeInsert / StartAction
    /// companions); only flags carrying InsertText|DeleteText are real
    /// content changes, so one operation yields exactly one version bump.
    /// Guarded while the backend itself mutates the document.
    void onEditorModified(Scintilla::ModificationFlags type);

private:
    void loadFromDisk();
    void saveTo(const std::filesystem::path& path);
    void setDocumentText(const std::string& bytes);

    ScintillaEditBase& editor_;
    std::filesystem::path path_;
    std::uint64_t bomOffset_ = 0;
    mqt::core::DocumentInfo info_{};
    mqt::core::DocumentVersion version_ = mqt::core::kInitialDocumentVersion;
    bool mutatingDocument_ = false;
};

} // namespace mqt::backend
