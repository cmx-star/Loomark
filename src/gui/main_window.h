#pragma once

#include "core/document_file.h"

#include <QMainWindow>

#include <filesystem>

class QAction;
class QLabel;
class QPlainTextEdit;
class QTextBrowser;
class QTimer;

namespace mqt::gui {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(const std::filesystem::path& initialPath = {}, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void openDocument();
    void saveDocument();
    void saveDocumentAs();
    void reloadDocument();
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

    std::filesystem::path currentPath_;
    mqt::core::FileInfo currentInfo_{};
    bool dirty_ = false;
    int previewStartLine_ = 1;
    int previewEndLine_ = 1;

    QPlainTextEdit* editor_ = nullptr;
    QTextBrowser* preview_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* sizeLabel_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QLabel* tierLabel_ = nullptr;
    QLabel* editorMetaLabel_ = nullptr;
    QLabel* previewMetaLabel_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* reloadAction_ = nullptr;
    QTimer* previewTimer_ = nullptr;
};

} // namespace mqt::gui
