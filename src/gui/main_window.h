#pragma once

#include "core/document_file.h"

#include <QMainWindow>

#include <atomic>
#include <filesystem>

class QAction;
class QCheckBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QTextBrowser;
class QTimer;
class QToolBar;
class QStackedWidget;
class QTabBar;
class ScintillaEditBase;

namespace mqt::gui {
class DocumentSession;
}

namespace mqt::backend {
class ScintillaDocumentBackend;
}

namespace mqt::gui {

class PreviewIndexThread;
class FileInspectThread;
#ifdef MQT_BUILD_TESTS
class MainWindowTestAccess;
#endif
class UpdateChecker;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(const std::filesystem::path& initialPath = {}, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
#ifdef MQT_BUILD_TESTS
    friend class MainWindowTestAccess;
#endif

    void openDocument();
    void saveDocument();
    void saveDocumentAs();
    void reloadDocument();
    void toggleFindBar();
    void findNext();
    void replaceAll();
    // M14 标签页
    void activateSession(mqt::gui::DocumentSession* session);
    bool closeSession(mqt::gui::DocumentSession* session);
    void updateTabForSession(mqt::gui::DocumentSession* session);
    [[nodiscard]] QString tabTextForSession(mqt::gui::DocumentSession* session) const;
    void enterEmptyState();
    [[nodiscard]] mqt::gui::DocumentSession* sessionForPath(
        const std::filesystem::path& path) const;
    void checkForUpdates(bool userInitiated);
    void openLogDirectory();
    bool loadDocument(const std::filesystem::path& path);
    bool maybeSaveChanges();
    void refreshPreview();
    void syncPreviewWithEditorScroll();
    QString collectPreviewText();
    bool writeCurrentDocument(const std::filesystem::path& path);
    void applyUiStyle();
    void updatePreviewLayout();
    void updateWindowState();
    void updateStatusBar();
    void setCurrentPath(std::filesystem::path path, const mqt::core::FileInfo& info);

    // Large-file (windowed) support.
    [[nodiscard]] bool windowed() const;
    void applyTierUiMode();
    bool loadIntoEditor(const std::string& raw);
    void closeActiveSession();
    int editorCharacterCount() const;
    int editorLineCount() const;
    bool readWindow(std::uint64_t rawStart);
    void jumpToWindowStart(std::uint64_t rawStart);
    void jumpToFirstWindow();
    void jumpToPreviousWindow();
    void jumpToNextWindow();
    void jumpToLastWindow();
    void jumpToPositionDialog();
    void requestIndexedPreview();
    void launchIndexThread(std::uint64_t generation);
    void applyIndexedPreviewResult(const PreviewIndexThread& thread);
    // Background whole-file inspection (newline style) for the open document.
    void launchInspectThread();
    void finishInspectThread();
    void completeInspection();
    void applyInspectedInfo(const mqt::core::FileInfo& info);
    void shutdownBackgroundWork();
    void ensureDiskSpace(const std::filesystem::path& path, std::uint64_t neededBytes) const;

    std::filesystem::path currentPath_;
    mqt::core::FileInfo currentInfo_{};
    bool dirty_ = false;
    int previewStartLine_ = 1;
    int previewEndLine_ = 1;
    bool largeMode_ = false;
    std::uint64_t windowStart_ = 0;
    std::uint64_t windowEnd_ = 0;
    std::uint64_t previewGeneration_ = 0;
    bool indexedPreviewPending_ = false;
    PreviewIndexThread* indexThread_ = nullptr;
    FileInspectThread* inspectThread_ = nullptr;
    std::uint64_t documentGeneration_ = 0;
    bool backgroundLoadPending_ = false;
    QMenu* windowMenu_ = nullptr;

    // M13/M14: each open document lives in its own DocumentSession (own
    // editor + backend); the tab bar switches between them. The
    // QPlainTextEdit editor remains the windowed large-file surface and the
    // empty-state fallback.
    QStackedWidget* editorStack_ = nullptr;
    QList<DocumentSession*> sessions_;
    QTabBar* tabBar_ = nullptr;
    DocumentSession* activeSession_ = nullptr;

    QPlainTextEdit* editor_ = nullptr;
    QTextBrowser* preview_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* sizeLabel_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QLabel* tierLabel_ = nullptr;
    QLabel* editorMetaLabel_ = nullptr;
    QLabel* previewMetaLabel_ = nullptr;
    // M11 查找/替换栏（Normal 档，Scintilla 编辑器）
    QToolBar* findBar_ = nullptr;
    QLineEdit* findEdit_ = nullptr;
    QLineEdit* replaceEdit_ = nullptr;
    QCheckBox* findRegex_ = nullptr;
    QCheckBox* findCase_ = nullptr;
    QLabel* findStatusLabel_ = nullptr;
    QAction* findToggleAction_ = nullptr;
    std::atomic_bool findCancelled_{false};

    QAction* saveAction_ = nullptr;
    QAction* reloadAction_ = nullptr;
    QAction* checkUpdatesAction_ = nullptr;
    QTimer* previewTimer_ = nullptr;
    UpdateChecker* updateChecker_ = nullptr;
    bool updateCheckUserInitiated_ = false;
};

} // namespace mqt::gui
