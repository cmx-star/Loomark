#include "gui/main_window.h"

#include "gui/markdown_document_renderer.h"

#include "core/file_tier.h"
#include "core/markdown_index.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFrame>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QMenuBar>
#include <QKeySequence>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSize>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextOption>
#include <QToolBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <exception>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace mqt::gui {
namespace {

constexpr int kFullPreviewCharLimit = 256 * 1024;
constexpr int kLargePreviewCharLimit = 128 * 1024;

class MarkdownEditor final : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;

    QTextBlock visibleTopBlock() const
    {
        return firstVisibleBlock();
    }
};

QString toQString(const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(u8.data()), static_cast<int>(u8.size()));
}

std::filesystem::path toPath(const QString& text)
{
    const auto utf8 = text.toUtf8();
    return std::filesystem::path(std::u8string_view(
        reinterpret_cast<const char8_t*>(utf8.constData()),
        static_cast<std::size_t>(utf8.size())));
}

QString formatFileSize(std::uint64_t sizeBytes)
{
    constexpr double kib = 1024.0;
    constexpr double mib = kib * 1024.0;
    constexpr double gib = mib * 1024.0;
    const auto value = static_cast<double>(sizeBytes);
    if (value >= gib) {
        return QStringLiteral("%1 GiB").arg(value / gib, 0, 'f', 2);
    }
    if (value >= mib) {
        return QStringLiteral("%1 MiB").arg(value / mib, 0, 'f', 1);
    }
    if (value >= kib) {
        return QStringLiteral("%1 KiB").arg(value / kib, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(sizeBytes);
}

std::string normalizeLineEndings(std::string text, mqt::core::NewlineStyle style)
{
    if (style == mqt::core::NewlineStyle::CRLF) {
        std::string converted;
        converted.reserve(text.size() + text.size() / 8);
        for (char ch : text) {
            if (ch == '\n') {
                converted.push_back('\r');
                converted.push_back('\n');
            } else {
                converted.push_back(ch);
            }
        }
        return converted;
    }
    if (style == mqt::core::NewlineStyle::CR) {
        std::string converted;
        converted.reserve(text.size());
        for (char ch : text) {
            if (ch == '\n') {
                converted.push_back('\r');
            } else {
                converted.push_back(ch);
            }
        }
        return converted;
    }
    return text;
}

QString makeTitle(const std::filesystem::path& path, bool dirty)
{
    QString title = QStringLiteral("Loomark");
    if (!path.empty()) {
        title += QStringLiteral(" - ");
        title += toQString(path.filename());
    }
    if (dirty) {
        title += QStringLiteral(" *");
    }
    return title;
}

QString fileDialogPath(const std::filesystem::path& path)
{
    if (path.empty()) {
        return {};
    }
    return toQString(path.parent_path());
}

QString compactPath(const std::filesystem::path& path)
{
    QString text = toQString(path);
    constexpr int maxChars = 92;
    if (text.size() <= maxChars) {
        return text;
    }
    return text.left(34) + QStringLiteral("...") + text.right(maxChars - 37);
}

std::string bomPrefix()
{
    return std::string("\xEF\xBB\xBF", 3);
}

QFont interfaceFont(int pointSize = 13)
{
    auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSize(pointSize);
    return font;
}

QFont editorFont()
{
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(13);
    font.setStyleHint(QFont::Monospace);
    return font;
}

bool isFenceLine(QStringView line)
{
    line = line.trimmed();
    return line.startsWith(QStringLiteral("```")) || line.startsWith(QStringLiteral("~~~"));
}

bool isInsideFenceBefore(QTextBlock block)
{
    bool insideFence = false;
    for (QTextBlock current = block.document()->begin(); current.isValid() && current != block; current = current.next()) {
        if (isFenceLine(QStringView(current.text()))) {
            insideFence = !insideFence;
        }
    }
    return insideFence;
}

QIcon themedIcon(QStringView name)
{
    return QIcon(QStringLiteral(":/qdarktheme/dist/dark/svg/%1__icon-foreground__rotate-0.svg").arg(name));
}

QWidget* makePanel(const QString& title, QLabel*& metaLabel, QWidget* body, QWidget* parent)
{
    auto* panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("Panel"));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(panel);
    header->setObjectName(QStringLiteral("PanelHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(14, 8, 14, 8);
    headerLayout->setSpacing(12);

    auto* titleLabel = new QLabel(title, header);
    titleLabel->setObjectName(QStringLiteral("PanelTitle"));
    metaLabel = new QLabel(header);
    metaLabel->setObjectName(QStringLiteral("PanelMeta"));
    metaLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    metaLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(metaLabel);

    layout->addWidget(header);
    layout->addWidget(body, 1);
    return panel;
}

} // namespace

MainWindow::MainWindow(const std::filesystem::path& initialPath, QWidget* parent)
    : QMainWindow(parent)
{
    setMinimumSize(980, 680);
    resize(1380, 860);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    editor_ = new MarkdownEditor(splitter);
    preview_ = new QTextBrowser(splitter);
    editor_->setPlaceholderText(QStringLiteral("打开文件开始编辑..."));
    editor_->setFrameShape(QFrame::NoFrame);
    editor_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    editor_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    editor_->setFont(editorFont());
    editor_->setTabStopDistance(editor_->fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    editor_->document()->setDocumentMargin(18);

    preview_->setFrameShape(QFrame::NoFrame);
    preview_->setOpenExternalLinks(true);
    preview_->setFont(interfaceFont(14));
    preview_->setLineWrapMode(QTextEdit::WidgetWidth);
    preview_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    preview_->document()->setDocumentMargin(22);

    splitter->addWidget(makePanel(QStringLiteral("源码"), editorMetaLabel_, editor_, splitter));
    splitter->addWidget(makePanel(QStringLiteral("预览"), previewMetaLabel_, preview_, splitter));
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({680, 700});
    splitter->setHandleWidth(1);
    setCentralWidget(splitter);

    auto* fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    auto* toolBar = addToolBar(QStringLiteral("文件"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolBar->setIconSize(QSize(20, 20));

    auto* openAction = new QAction(themedIcon(QStringLiteral("folder_open")), QStringLiteral("打开"), this);
    saveAction_ = new QAction(themedIcon(QStringLiteral("save")), QStringLiteral("保存"), this);
    auto* saveAsAction = new QAction(themedIcon(QStringLiteral("create_new_folder")), QStringLiteral("另存为"), this);
    reloadAction_ = new QAction(themedIcon(QStringLiteral("restart_alt")), QStringLiteral("重新载入"), this);
    auto* quitAction = new QAction(QStringLiteral("退出"), this);

    openAction->setShortcut(QKeySequence::Open);
    saveAction_->setShortcut(QKeySequence::Save);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    reloadAction_->setShortcut(QKeySequence::Refresh);
    quitAction->setShortcut(QKeySequence::Quit);

    connect(openAction, &QAction::triggered, this, &MainWindow::openDocument);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveDocument);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveDocumentAs);
    connect(reloadAction_, &QAction::triggered, this, &MainWindow::reloadDocument);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
    connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
        dirty_ = true;
        updateWindowState();
        updateStatusBar();
        if (previewTimer_) {
            previewTimer_->start();
        }
    });
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        updateStatusBar();
    });
    connect(editor_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        syncPreviewWithEditorScroll();
        updateStatusBar();
    });

    fileMenu->addAction(openAction);
    fileMenu->addAction(saveAction_);
    fileMenu->addAction(saveAsAction);
    fileMenu->addAction(reloadAction_);
    fileMenu->addSeparator();
    fileMenu->addAction(quitAction);

    toolBar->addAction(openAction);
    toolBar->addAction(saveAction_);
    toolBar->addAction(saveAsAction);
    toolBar->addAction(reloadAction_);

    auto* status = statusBar();
    pathLabel_ = new QLabel(status);
    sizeLabel_ = new QLabel(status);
    tierLabel_ = new QLabel(status);
    stateLabel_ = new QLabel(status);
    status->addPermanentWidget(pathLabel_, 1);
    status->addPermanentWidget(sizeLabel_);
    status->addPermanentWidget(tierLabel_);
    status->addPermanentWidget(stateLabel_);

    previewTimer_ = new QTimer(this);
    previewTimer_->setSingleShot(true);
    previewTimer_->setInterval(60);
    connect(previewTimer_, &QTimer::timeout, this, &MainWindow::refreshPreview);

    applyUiStyle();
    updatePreviewLayout();
    saveAction_->setEnabled(false);
    reloadAction_->setEnabled(false);
    updateWindowState();
    updateStatusBar();
    preview_->clear();
    status->showMessage(QStringLiteral("就绪"));
    if (!initialPath.empty()) {
        loadDocument(initialPath);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSaveChanges()) {
        event->accept();
        return;
    }
    event->ignore();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updatePreviewLayout();
}

void MainWindow::openDocument()
{
    if (!maybeSaveChanges()) {
        return;
    }

    const auto fileName = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("打开 Markdown 文件"),
        fileDialogPath(currentPath_),
        QStringLiteral("Markdown 文件 (*.md *.markdown *.txt);;所有文件 (*.*)"));
    if (fileName.isEmpty()) {
        return;
    }

    loadDocument(toPath(fileName));
}

void MainWindow::saveDocument()
{
    if (currentPath_.empty()) {
        saveDocumentAs();
        return;
    }
    writeCurrentDocument(currentPath_);
}

void MainWindow::saveDocumentAs()
{
    const auto defaultPath = currentPath_.empty() ? QString() : toQString(currentPath_);
    const auto fileName = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("另存为"),
        defaultPath,
        QStringLiteral("Markdown 文件 (*.md *.markdown *.txt);;所有文件 (*.*)"));
    if (fileName.isEmpty()) {
        return;
    }

    writeCurrentDocument(toPath(fileName));
}

void MainWindow::reloadDocument()
{
    if (currentPath_.empty()) {
        return;
    }
    if (!maybeSaveChanges()) {
        return;
    }
    loadDocument(currentPath_);
}

bool MainWindow::loadDocument(const std::filesystem::path& path)
{
    try {
        const auto info = mqt::core::inspectFile(path);
        if (info.tier == mqt::core::FileTier::Reject) {
            QMessageBox::warning(
                this,
                QStringLiteral("文件过大"),
                QStringLiteral("这个文件已经超过当前首版支持上限，暂时无法打开。"));
            return false;
        }

        std::string raw = mqt::core::readRange(path, {0, info.sizeBytes});
        if (info.hasUtf8Bom && raw.rfind(bomPrefix(), 0) == 0) {
            raw.erase(0, 3);
        }

        const QString text = QString::fromUtf8(raw.data(), static_cast<int>(raw.size()));
        {
            const QSignalBlocker blocker(editor_);
            editor_->setPlainText(text);
            editor_->document()->setModified(false);
        }

        setCurrentPath(path, info);
        dirty_ = false;
        refreshPreview();
        updateWindowState();
        updateStatusBar();
        statusBar()->showMessage(QStringLiteral("已打开 %1").arg(toQString(path)), 2000);
        return true;
    } catch (const std::exception& error) {
        QMessageBox::critical(this, QStringLiteral("打开失败"), QString::fromUtf8(error.what()));
        return false;
    }
}

bool MainWindow::maybeSaveChanges()
{
    if (!dirty_) {
        return true;
    }

    const auto choice = QMessageBox::warning(
        this,
        QStringLiteral("未保存更改"),
        QStringLiteral("当前文档有未保存的修改，是否保存？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (choice == QMessageBox::Save) {
        saveDocument();
        return !dirty_;
    }
    if (choice == QMessageBox::Cancel) {
        return false;
    }
    return true;
}

void MainWindow::refreshPreview()
{
    updatePreviewLayout();
    const QString previewText = collectPreviewText();
    QUrl baseUrl;
    if (!currentPath_.empty()) {
        QString basePath = toQString(currentPath_.parent_path());
        if (!basePath.endsWith(QLatin1Char('/'))) {
            basePath += QLatin1Char('/');
        }
        baseUrl = QUrl::fromLocalFile(basePath);
    }
    const auto renderResult = renderMarkdownDocument(*preview_->document(), previewText, baseUrl);
    updatePreviewLayout();

    const int characterCount = std::max(0, editor_->document()->characterCount() - 1);
    if (!renderResult.success) {
        previewMetaLabel_->setText(QStringLiteral("预览失败 · %1").arg(renderResult.errorMessage.left(72)));
    } else if (characterCount > kFullPreviewCharLimit) {
        preview_->verticalScrollBar()->setValue(0);
        previewMetaLabel_->setText(QStringLiteral("大型文件 · 第 %1-%2 行 / 共 %3 行")
            .arg(previewStartLine_)
            .arg(previewEndLine_)
            .arg(editor_->document()->blockCount()));
    } else {
        previewMetaLabel_->setText(QStringLiteral("整篇 · %1").arg(formatFileSize(currentInfo_.sizeBytes)));
        syncPreviewWithEditorScroll();
    }
}

void MainWindow::syncPreviewWithEditorScroll()
{
    const int characterCount = std::max(0, editor_->document()->characterCount() - 1);
    if (characterCount > kFullPreviewCharLimit) {
        if (previewTimer_) {
            previewTimer_->start();
        }
        return;
    }

    auto* editorScroll = editor_->verticalScrollBar();
    auto* previewScroll = preview_->verticalScrollBar();
    if (editorScroll->maximum() <= 0 || previewScroll->maximum() <= 0) {
        return;
    }

    const double ratio = static_cast<double>(editorScroll->value()) / static_cast<double>(editorScroll->maximum());
    const QSignalBlocker blocker(previewScroll);
    previewScroll->setValue(static_cast<int>(ratio * previewScroll->maximum()));
}

QString MainWindow::collectPreviewText()
{
    const auto* document = editor_->document();
    const int characterCount = std::max(0, document->characterCount() - 1);
    if (characterCount <= kFullPreviewCharLimit) {
        previewStartLine_ = 1;
        previewEndLine_ = document->blockCount();
        return editor_->toPlainText();
    }

    QTextBlock block = static_cast<MarkdownEditor*>(editor_)->visibleTopBlock();
    if (!block.isValid()) {
        block = document->begin();
    }

    QString text;
    text.reserve(kLargePreviewCharLimit);
    bool fenceOpen = isInsideFenceBefore(block);
    previewStartLine_ = block.isValid() ? block.blockNumber() + 1 : 1;
    previewEndLine_ = previewStartLine_;
    if (fenceOpen) {
        text += QStringLiteral("```\n");
    }
    while (block.isValid() && text.size() < kLargePreviewCharLimit) {
        QString line = block.text();
        const int remaining = kLargePreviewCharLimit - text.size();
        if (line.size() + 1 > remaining) {
            if (!text.isEmpty()) {
                break;
            }
            line.truncate(std::max(0, remaining - 1));
        }
        text += line;
        text += QLatin1Char('\n');
        if (isFenceLine(QStringView(line))) {
            fenceOpen = !fenceOpen;
        }
        previewEndLine_ = block.blockNumber() + 1;
        block = block.next();
    }
    if (fenceOpen) {
        text += QStringLiteral("```\n");
    }
    return text;
}

bool MainWindow::writeCurrentDocument(const std::filesystem::path& path)
{
    try {
        const auto text = editor_->toPlainText();
        QByteArray utf8 = text.toUtf8();
        std::string data(utf8.constData(), static_cast<std::size_t>(utf8.size()));
        data = normalizeLineEndings(std::move(data), currentInfo_.newlineStyle);
        if (currentInfo_.hasUtf8Bom) {
            data.insert(0, bomPrefix());
        }

        mqt::core::writeFileAtomically(path, data);
        const auto info = mqt::core::inspectFile(path);
        setCurrentPath(path, info);
        dirty_ = false;
        editor_->document()->setModified(false);
        updateWindowState();
        updateStatusBar();
        statusBar()->showMessage(QStringLiteral("已保存 %1").arg(toQString(path)), 2000);
        return true;
    } catch (const std::exception& error) {
        QMessageBox::critical(this, QStringLiteral("保存失败"), QString::fromUtf8(error.what()));
        return false;
    }
}

void MainWindow::updateWindowState()
{
    setWindowTitle(makeTitle(currentPath_, dirty_));
    saveAction_->setEnabled(dirty_);
    reloadAction_->setEnabled(!currentPath_.empty());
}

void MainWindow::applyUiStyle()
{
    setFont(interfaceFont());
    setStyleSheet(QStringLiteral(R"(
        QToolBar {
            spacing: 8px;
            padding: 8px 12px;
        }
        QToolButton {
            padding: 7px 12px;
            min-height: 24px;
        }
        QSplitter {
            background: #1f2024;
        }
        QSplitter::handle {
            background: #34363d;
        }
        QFrame#Panel {
            background: #202124;
            border: 1px solid #34363d;
            border-radius: 8px;
        }
        QWidget#PanelHeader {
            background: #26282d;
            border-top-left-radius: 8px;
            border-top-right-radius: 8px;
            border-bottom: 1px solid #34363d;
        }
        QLabel#PanelTitle {
            color: #ffffff;
            font-size: 14px;
            font-weight: 700;
        }
        QLabel#PanelMeta {
            color: #b6bbc5;
            font-size: 12px;
        }
        QPlainTextEdit, QTextBrowser {
            background: #1b1c20;
            color: #e8eaed;
            border: none;
            border-bottom-left-radius: 8px;
            border-bottom-right-radius: 8px;
        }
        QPlainTextEdit {
            font-size: 13px;
        }
        QTextBrowser {
            font-size: 14px;
        }
        QStatusBar {
            padding: 4px 10px;
            font-size: 12px;
        }
        QStatusBar QLabel {
            font-size: 12px;
            padding: 0 6px;
        }
    )"));
    statusBar()->setSizeGripEnabled(false);
}

void MainWindow::updatePreviewLayout()
{
    if (!preview_) {
        return;
    }

    const int width = preview_->viewport()->width();
    int pointSize = 14;
    int margin = 22;
    if (width < 520) {
        pointSize = 13;
        margin = 16;
    } else if (width > 860) {
        pointSize = 15;
        margin = 30;
    }

    auto font = interfaceFont(pointSize);
    preview_->setFont(font);
    preview_->document()->setDefaultFont(font);
    preview_->document()->setDocumentMargin(margin);
    preview_->document()->setTextWidth(std::max(320, width - margin * 2));
}

void MainWindow::updateStatusBar()
{
    const int line = editor_->textCursor().blockNumber() + 1;
    const int column = editor_->textCursor().positionInBlock() + 1;
    const int characterCount = std::max(0, editor_->document()->characterCount() - 1);
    editorMetaLabel_->setText(QStringLiteral("%1 行 · %2 字符 · 光标 %3:%4")
        .arg(editor_->document()->blockCount())
        .arg(characterCount)
        .arg(line)
        .arg(column));

    if (currentPath_.empty()) {
        pathLabel_->setText(QStringLiteral("文件: 未打开"));
        sizeLabel_->setText(QStringLiteral("大小: -"));
        tierLabel_->setText(QStringLiteral("档位: -"));
        pathLabel_->setToolTip(QString());
    } else {
        pathLabel_->setText(QStringLiteral("文件: %1").arg(compactPath(currentPath_)));
        pathLabel_->setToolTip(toQString(currentPath_));
        sizeLabel_->setText(QStringLiteral("大小: %1").arg(formatFileSize(currentInfo_.sizeBytes)));
        tierLabel_->setText(QStringLiteral("档位: %1").arg(QString::fromUtf8(mqt::core::toString(currentInfo_.tier))));
    }
    stateLabel_->setText(dirty_ ? QStringLiteral("状态: 已修改") : QStringLiteral("状态: 已保存"));
}

void MainWindow::setCurrentPath(std::filesystem::path path, const mqt::core::FileInfo& info)
{
    currentPath_ = std::move(path);
    currentInfo_ = info;
}

} // namespace mqt::gui
