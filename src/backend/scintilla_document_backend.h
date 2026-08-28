#pragma once

#include "core/document_backend.h"
#include "core/load_scanner.h"

#include <ScintillaEditBase.h>

#include <QObject>
#include <filesystem>

namespace mqt::backend {

class ScintillaLoadTask;

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
    explicit ScintillaDocumentBackend(ScintillaEditBase& editor);
    /// Attach to an editor and load `path` synchronously. Throws for
    /// non-Normal tier files (M09 scope; use startBackgroundLoad for large
    /// documents).
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

    /// M10: attach `path` and load it through the background chunked loader.
    /// UI thread stays responsive; consume loadProgress()/loadFinished().
    /// Cancelled or failed loads leave an empty buffer.
    bool startBackgroundLoad(const std::filesystem::path& path,
        std::uint64_t blockSize = 0);
    void cancelBackgroundLoad();
    [[nodiscard]] bool isLoadInProgress() const { return loadTask_ != nullptr; }

    /// Synchronous load of a small document (tests and tiny files).
    void openSync(const std::filesystem::path& path)
    {
        path_ = path;
        loadFromDisk();
    }

    /// Sparse line index and streaming fingerprint of the loaded content
    /// (populated after a completed load; empty/0 before).
    [[nodiscard]] const mqt::core::SparseLineIndex& lineIndex() const { return lineIndex_; }
    [[nodiscard]] std::uint64_t fingerprint() const { return fingerprint_; }

    // ---- M12: save point, undo budget, external fingerprint check ----

    /// True when the version drifted from the last save/load point.
    [[nodiscard]] bool isDirty() const { return version_ != savedVersion_; }

    /// Undo memory budget (bytes of inserted+deleted content tracked in the
    /// undo history estimate). Exceeding it clears the undo history and emits
    /// undoBudgetExceeded(); the on-disk save point is unaffected.
    void setUndoBudgetBytes(std::uint64_t bytes) { undoBudgetBytes_ = bytes; }
    [[nodiscard]] std::uint64_t undoBudgetBytes() const { return undoBudgetBytes_; }
    [[nodiscard]] bool canUndo() const { return editor_.send(SCI_CANUNDO) != 0; }

    // ---- M11: batched search & confirmed bulk replace ----

    struct SearchBatchRequest {
        std::string needle;
        bool regex = false;
        bool matchCase = true;
        bool wholeWord = false;
        std::uint64_t startOffset = 0;
        std::uint32_t maxResults = 1000;
        std::uint64_t maxWindow = 1ULL << 20; // per-batch scan window cap
        std::uint64_t deadlineMs = 250;       // per-batch time budget
    };

    struct SearchBatchResult {
        std::vector<mqt::core::SearchHit> hits;
        bool truncated = false;  // maxResults reached, more matches may follow
        bool timeout = false;    // per-batch deadline exceeded
        bool exhausted = false;  // scanned through the end of the document
        bool cancelled = false;
        std::uint64_t nextOffset = 0; // resume point for the next batch
    };

    SearchBatchResult searchBatch(const SearchBatchRequest& request,
        const std::atomic_bool* cancelFlag = nullptr) const;

    /// Bulk replace with the >32MiB confirmation gate. Delegates to apply(),
    /// so version / range / overlap semantics and the single undo group are
    /// identical to programmatic edits.
    mqt::core::ApplyResult applyReplace(const std::vector<mqt::core::TextEdit>& edits,
        mqt::core::DocumentVersion baseVersion, bool confirmed);

signals:
    void loadProgress(std::uint64_t loadedBytes, std::uint64_t totalBytes);
    void loadFinished(bool ok, const QString& error);
    void undoBudgetExceeded();
    /// Emitted for user-driven content changes (not backend apply/load).
    void contentsChanged();

private slots:
    void onLoadChunk(const QByteArray& bytes);
    void onLoadTaskFinished(bool ok, const QString& error);
    /// Any buffer change that did not go through apply()/load bumps the
    /// version (user typing, undo, paste, cut, ...). Scintilla emits several
    /// notifications per operation (InsertCheck / BeforeInsert / StartAction
    /// companions); only flags carrying InsertText|DeleteText are real
    /// content changes, so one operation yields exactly one version bump.
    /// Guarded while the backend itself mutates the document.
    void onEditorModified(Scintilla::ModificationFlags type,
        Scintilla::Position position, Scintilla::Position length);
    /// Account a backend-driven edit into the undo memory estimate.
    void chargeUndoBytes(std::uint64_t bytes);

private:
    void loadFromDisk();
    void saveTo(const std::filesystem::path& path);
    void setDocumentText(const std::string& bytes);

    ScintillaEditBase& editor_;
    ScintillaLoadTask* loadTask_ = nullptr;
    mqt::core::SparseLineIndex lineIndex_;
    std::uint64_t fingerprint_ = 0;
    // M12: save point & external-change baseline
    mqt::core::DocumentVersion savedVersion_ = mqt::core::kInitialDocumentVersion;
    std::uint64_t baselineFingerprint_ = 0;
    std::uint64_t undoBudgetBytes_ = 64ULL << 20;
    std::uint64_t undoBytesUsed_ = 0;
    std::filesystem::path path_;
    std::uint64_t bomOffset_ = 0;
    mqt::core::DocumentInfo info_{};
    mqt::core::DocumentVersion version_ = mqt::core::kInitialDocumentVersion;
    bool mutatingDocument_ = false;
};

} // namespace mqt::backend
