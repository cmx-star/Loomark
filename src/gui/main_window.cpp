#include "gui/main_window.h"

#include "gui/application_logger.h"
#include "gui/markdown_document_renderer.h"
#include "gui/preview_index_worker.h"
#include "gui/update_checker.h"

#include "core/file_tier.h"
#include "core/markdown_index.h"

#include <QAction>
#include <QApplication>
#include <QAbstractButton>
#include <QCloseEvent>
#include <QDebug>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFrame>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMenuBar>
#include <QKeySequence>
#include <QPlainTextEdit>
#include <QPushButton>
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
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace mqt::gui {
namespace {

constexpr int kFullPreviewCharLimit = 256 * 1024;
constexpr int kLargePreviewCharLimit = 128 * 1024;
constexpr std::uint64_t kWindowBytes = 2ULL * 1024ULL * 1024ULL;
constexpr int kMaxIndexedPreviewChars = 192 * 1024;

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

// Returns a cut position <= data.size() that does not split a UTF-8
// multi-byte sequence at the end of the buffer.
std::size_t alignUtf8Cut(const std::string& data)
{
    if (data.empty()) {
        return 0;
    }
    std::size_t cut = data.size();
    std::size_t scanned = 0;
    const std::size_t limit = std::min<std::size_t>(3, cut);
    while (scanned < limit) {
        const auto ch = static_cast<unsigned char>(data[cut - 1]);
        if ((ch & 0xC0) == 0x80) { // continuation byte inside a split sequence
            --cut;
            ++scanned;
            continue;
        }
        std::size_t seqLen = 1;
        if ((ch & 0xE0) == 0xC0) {
            seqLen = 2;
        } else if ((ch & 0xF0) == 0xE0) {
            seqLen = 3;
        } else if ((ch & 0xF8) == 0xF0) {
            seqLen = 4;
        }
        if (seqLen > scanned + 1) {
            --cut; // trailing lead byte whose sequence is incomplete
        }
        break;
    }
    return cut;
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
    auto* helpMenu = menuBar()->addMenu(QStringLiteral("帮助"));
    auto* toolBar = addToolBar(QStringLiteral("文件"));
    toolBar->setMovable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolBar->setIconSize(QSize(20, 20));

    auto* openAction = new QAction(themedIcon(QStringLiteral("folder_open")), QStringLiteral("打开"), this);
    saveAction_ = new QAction(themedIcon(QStringLiteral("save")), QStringLiteral("保存"), this);
    auto* saveAsAction = new QAction(themedIcon(QStringLiteral("create_new_folder")), QStringLiteral("另存为"), this);
    reloadAction_ = new QAction(themedIcon(QStringLiteral("restart_alt")), QStringLiteral("重新载入"), this);
    auto* quitAction = new QAction(QStringLiteral("退出"), this);
    checkUpdatesAction_ = new QAction(QStringLiteral("检查更新"), this);
    auto* openLogDirectoryAction = new QAction(QStringLiteral("打开日志目录"), this);

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
    connect(checkUpdatesAction_, &QAction::triggered, this, [this] {
        checkForUpdates(true);
    });
    connect(openLogDirectoryAction, &QAction::triggered, this, &MainWindow::openLogDirectory);
    connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
        dirty_ = true;
        updateWindowState();
        updateStatusBar();
        if (previewTimer_ && !largeMode_) {
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
    helpMenu->addAction(checkUpdatesAction_);
    helpMenu->addAction(openLogDirectoryAction);

    windowMenu_ = menuBar()->addMenu(QStringLiteral("窗口"));
    auto* winFirstAction = new QAction(QStringLiteral("跳到文件开头"), this);
    auto* winPrevAction = new QAction(QStringLiteral("上一窗口"), this);
    auto* winNextAction = new QAction(QStringLiteral("下一窗口"), this);
    auto* winLastAction = new QAction(QStringLiteral("跳到文件末尾"), this);
    auto* winJumpAction = new QAction(QStringLiteral("跳转到指定位置…"), this);
    connect(winFirstAction, &QAction::triggered, this, &MainWindow::jumpToFirstWindow);
    connect(winPrevAction, &QAction::triggered, this, &MainWindow::jumpToPreviousWindow);
    connect(winNextAction, &QAction::triggered, this, &MainWindow::jumpToNextWindow);
    connect(winLastAction, &QAction::triggered, this, &MainWindow::jumpToLastWindow);
    connect(winJumpAction, &QAction::triggered, this, &MainWindow::jumpToPositionDialog);
    windowMenu_->addAction(winFirstAction);
    windowMenu_->addAction(winPrevAction);
    windowMenu_->addAction(winNextAction);
    windowMenu_->addAction(winLastAction);
    windowMenu_->addAction(winJumpAction);
    windowMenu_->setEnabled(false);

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

    updateChecker_ = new UpdateChecker(this);
    connect(updateChecker_, &UpdateChecker::updateAvailable, this, [this](const ReleaseInfo& release, const ReleaseAsset& asset) {
        checkUpdatesAction_->setEnabled(true);
        updateCheckUserInitiated_ = false;
        qInfo().noquote() << QStringLiteral("Update available: current=%1 latest=%2 release=%3")
            .arg(currentAppVersion(), release.version, release.releaseUrl.toString());

        QMessageBox box(this);
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle(QStringLiteral("发现新版本"));
        box.setText(QStringLiteral("Loomark %1 已发布。").arg(release.tagName));
        QString details = QStringLiteral("当前版本: %1\n最新版本: %2\n发布地址: %3")
            .arg(currentAppVersion(), release.version, release.releaseUrl.toString());
        if (!asset.name.isEmpty()) {
            details += QStringLiteral("\n下载文件: %1").arg(asset.name);
        }
        box.setInformativeText(details);
        QAbstractButton* downloadButton = box.addButton(asset.downloadUrl.isValid() ? QStringLiteral("下载更新") : QStringLiteral("打开发布页"), QMessageBox::AcceptRole);
        QAbstractButton* releaseButton = box.addButton(QStringLiteral("发布页"), QMessageBox::ActionRole);
        box.addButton(QStringLiteral("稍后"), QMessageBox::RejectRole);
        box.exec();

        if (box.clickedButton() == downloadButton) {
            QDesktopServices::openUrl(asset.downloadUrl.isValid() ? asset.downloadUrl : release.releaseUrl);
        } else if (box.clickedButton() == releaseButton) {
            QDesktopServices::openUrl(release.releaseUrl);
        }
    });
    connect(updateChecker_, &UpdateChecker::alreadyUpToDate, this, [this](const QString& latestVersion) {
        checkUpdatesAction_->setEnabled(true);
        if (updateCheckUserInitiated_) {
            QMessageBox::information(
                this,
                QStringLiteral("已是最新版本"),
                QStringLiteral("当前版本 %1 已是最新版本。GitHub 最新版本: %2。")
                    .arg(currentAppVersion(), latestVersion));
        }
        updateCheckUserInitiated_ = false;
        qInfo().noquote() << QStringLiteral("Update check complete: current=%1 latest=%2")
            .arg(currentAppVersion(), latestVersion);
    });
    connect(updateChecker_, &UpdateChecker::checkFailed, this, [this](const QString& message) {
        checkUpdatesAction_->setEnabled(true);
        if (updateCheckUserInitiated_) {
            QMessageBox::warning(this, QStringLiteral("检查更新失败"), message);
        }
        updateCheckUserInitiated_ = false;
        qWarning().noquote() << QStringLiteral("Update check failed: %1").arg(message);
    });

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
    QTimer::singleShot(1600, this, [this] {
        checkForUpdates(false);
    });
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

void MainWindow::checkForUpdates(bool userInitiated)
{
    if (!updateChecker_ || !checkUpdatesAction_) {
        return;
    }
    updateCheckUserInitiated_ = updateCheckUserInitiated_ || userInitiated;
    checkUpdatesAction_->setEnabled(false);
    if (userInitiated) {
        statusBar()->showMessage(QStringLiteral("正在检查 GitHub 更新..."), 2000);
    }
    qInfo().noquote() << QStringLiteral("Checking updates from %1").arg(githubLatestReleaseApiUrl());
    updateChecker_->checkNow();
}

void MainWindow::openLogDirectory()
{
    QString directory = applicationLogDirectory();
    if (directory.isEmpty()) {
        initializeApplicationLogging();
        directory = applicationLogDirectory();
    }
    if (directory.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("日志目录不可用"), QStringLiteral("当前无法创建本地日志目录。"));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}

bool MainWindow::loadDocument(const std::filesystem::path& path)
{
    try {
        const auto info = mqt::core::inspectFile(path);
        if (info.tier == mqt::core::FileTier::Reject) {
            QMessageBox::warning(
                this,
                QStringLiteral("文件过大"),
                QStringLiteral("文件超过 512 MiB 上限，当前版本暂不支持打开。"));
            return false;
        }

        largeMode_ = info.tier != mqt::core::FileTier::Normal;
        if (largeMode_) {
            const bool extreme = info.tier == mqt::core::FileTier::Extreme;
            const QString headline = extreme
                ? QStringLiteral("超大文件（256 ~ 512 MiB）")
                : QStringLiteral("大文件（64 ~ 256 MiB）");
            const QString body = QStringLiteral("将以%1打开：\n\n• 关闭自动折行\n• 编辑器按 2 MiB 窗口加载（“窗口”菜单切换）\n• 预览基于后台索引，反映已保存内容\n\n确定打开吗？")
                .arg(extreme ? QStringLiteral("受限模式") : QStringLiteral("大文件模式"));
            const auto choice = QMessageBox::question(this, headline, body,
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (choice != QMessageBox::Yes) {
                largeMode_ = false;
                return false;
            }
        }

        setCurrentPath(path, info);
        applyTierUiMode();

        if (!largeMode_) {
            std::string raw = mqt::core::readRange(path, {0, info.sizeBytes});
            if (info.hasUtf8Bom && raw.rfind(bomPrefix(), 0) == 0) {
                raw.erase(0, 3);
            }
            loadIntoEditor(raw);
            windowStart_ = 0;
            windowEnd_ = info.sizeBytes;
        } else if (!readWindow(0)) {
            QMessageBox::critical(this, QStringLiteral("打开失败"), QStringLiteral("无法读取文件内容。"));
            return false;
        }

        dirty_ = false;
        refreshPreview();
        updateWindowState();
        updateStatusBar();
        statusBar()->showMessage(QStringLiteral("已打开 %1").arg(toQString(path)), 2000);
        return true;
    } catch (const std::exception& error) {
        largeMode_ = false;
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
    if (windowed()) {
        requestIndexedPreview();
        return;
    }
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
    if (windowed()) {
        // Indexed preview reflects the saved document; no scroll coupling.
        return;
    }

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

        if (windowed()) {
            // Splice save: original head + edited window + original tail.
            // Head/tail come from the source document on disk; BOM bytes stay
            // in the untouched head, so no extra BOM is inserted here.
            const std::filesystem::path sourcePath = currentPath_;
            std::string payload;
            payload.reserve(data.size() * 2);
            if (windowStart_ > 0) {
                payload += mqt::core::readRange(sourcePath, {0, windowStart_});
            }
            data = normalizeLineEndings(std::move(data), currentInfo_.newlineStyle);
            payload += data;
            if (windowEnd_ < currentInfo_.sizeBytes) {
                payload += mqt::core::readRange(sourcePath, {windowEnd_, currentInfo_.sizeBytes});
            }

            ensureDiskSpace(path, payload.size());
            mqt::core::writeFileAtomically(path, payload);
            windowEnd_ = windowStart_ + data.size();
        } else {
            data = normalizeLineEndings(std::move(data), currentInfo_.newlineStyle);
            if (currentInfo_.hasUtf8Bom) {
                data.insert(0, bomPrefix());
            }

            ensureDiskSpace(path, data.size());
            mqt::core::writeFileAtomically(path, data);
        }

        const auto info = mqt::core::inspectFile(path);
        setCurrentPath(path, info);
        dirty_ = false;
        editor_->document()->setModified(false);
        updateWindowState();
        updateStatusBar();
        statusBar()->showMessage(QStringLiteral("已保存 %1").arg(toQString(path)), 2000);
        if (largeMode_) {
            refreshPreview(); // re-index the saved content
        }
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
    if (windowMenu_) {
        windowMenu_->setEnabled(windowed());
    }
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

    if (windowed()) {
        editorMetaLabel_->setText(editorMetaLabel_->text()
            + QStringLiteral(" · 窗口字节 %1–%2").arg(windowStart_).arg(windowEnd_));
    }

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

bool MainWindow::windowed() const
{
    return largeMode_ && !currentPath_.empty();
}

void MainWindow::applyTierUiMode()
{
    if (largeMode_) {
        editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
        editor_->setWordWrapMode(QTextOption::NoWrap);
    } else {
        editor_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        editor_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    }
}

bool MainWindow::loadIntoEditor(const std::string& raw)
{
    const QString text = QString::fromUtf8(raw.data(), static_cast<int>(raw.size()));
    {
        const QSignalBlocker blocker(editor_);
        editor_->setPlainText(text);
        editor_->document()->setModified(false);
    }
    return true;
}

bool MainWindow::readWindow(std::uint64_t rawStart)
{
    const bool bomHead = rawStart == 0 && currentInfo_.hasUtf8Bom;
    const std::uint64_t begin = rawStart + (bomHead ? bomPrefix().size() : 0);
    if (begin >= currentInfo_.sizeBytes) {
        return false;
    }
    const std::uint64_t want = std::min<std::uint64_t>(kWindowBytes, currentInfo_.sizeBytes - begin);
    std::string raw = mqt::core::readRange(currentPath_, {begin, begin + want});
    std::size_t cut = alignUtf8Cut(raw);
    if (currentInfo_.newlineStyle == mqt::core::NewlineStyle::CRLF && cut > 0 && raw[cut - 1] == '\r') {
        --cut; // keep CRLF pairs together across window seams
    }
    raw.resize(cut);
    windowStart_ = begin;
    windowEnd_ = begin + cut;
    return loadIntoEditor(raw);
}

void MainWindow::jumpToWindowStart(std::uint64_t rawStart)
{
    if (!windowed()) {
        return;
    }
    if (dirty_ && !maybeSaveChanges()) {
        return;
    }
    try {
        if (!readWindow(rawStart)) {
            statusBar()->showMessage(QStringLiteral("已到达文件边界"), 2000);
            return;
        }
    } catch (const std::exception& error) {
        QMessageBox::critical(this, QStringLiteral("读取失败"), QString::fromUtf8(error.what()));
        return;
    }
    dirty_ = false;
    updateWindowState();
    updateStatusBar();
    refreshPreview();
}

void MainWindow::jumpToFirstWindow()
{
    jumpToWindowStart(0);
}

void MainWindow::jumpToPreviousWindow()
{
    if (!windowed()) {
        return;
    }
    if (windowStart_ == 0 || (currentInfo_.hasUtf8Bom && windowStart_ <= 3)) {
        statusBar()->showMessage(QStringLiteral("已在文件开头"), 2000);
        return;
    }
    const std::uint64_t target = windowStart_ > kWindowBytes ? windowStart_ - kWindowBytes : 0;
    jumpToWindowStart(target);
}

void MainWindow::jumpToNextWindow()
{
    if (!windowed()) {
        return;
    }
    if (windowEnd_ >= currentInfo_.sizeBytes) {
        statusBar()->showMessage(QStringLiteral("已在文件末尾"), 2000);
        return;
    }
    jumpToWindowStart(windowEnd_);
}

void MainWindow::jumpToLastWindow()
{
    if (!windowed()) {
        return;
    }
    if (currentInfo_.sizeBytes > kWindowBytes) {
        jumpToWindowStart(currentInfo_.sizeBytes - kWindowBytes);
    } else {
        jumpToWindowStart(0);
    }
}

void MainWindow::jumpToPositionDialog()
{
    if (!windowed()) {
        return;
    }
    const double maxMib = static_cast<double>(currentInfo_.sizeBytes) / static_cast<double>(mqt::core::kMiB);
    bool ok = false;
    const double valueMib = QInputDialog::getDouble(
        this,
        QStringLiteral("跳转到指定位置"),
        QStringLiteral("位置（MiB，范围 0 ~ %1）：").arg(maxMib, 0, 'f', 1),
        static_cast<double>(windowStart_) / static_cast<double>(mqt::core::kMiB),
        0.0,
        maxMib,
        1,
        &ok);
    if (!ok) {
        return;
    }
    std::uint64_t target = static_cast<std::uint64_t>(valueMib * static_cast<double>(mqt::core::kMiB));
    const std::uint64_t maxStart = currentInfo_.sizeBytes > kWindowBytes
        ? currentInfo_.sizeBytes - kWindowBytes
        : 0;
    if (target > maxStart) {
        target = maxStart;
    }
    jumpToWindowStart(target);
}

void MainWindow::requestIndexedPreview()
{
    ++previewGeneration_;
    if (indexThread_) {
        indexedPreviewPending_ = true;
        previewMetaLabel_->setText(QStringLiteral("后台索引中…"));
        return;
    }
    launchIndexThread(previewGeneration_);
}

void MainWindow::launchIndexThread(std::uint64_t generation)
{
    mqt::core::BuildPreviewOptions options;
    options.maxBlocks = 800;
    indexThread_ = new PreviewIndexThread(currentPath_, options, generation, this);
    connect(indexThread_, &QThread::finished, this, [this]() {
        auto* thread = indexThread_;
        indexThread_ = nullptr;
        if (thread != nullptr) {
            thread->deleteLater();
            if (thread->generation() == previewGeneration_) {
                applyIndexedPreviewResult(*thread);
            }
        }
        if (indexedPreviewPending_) {
            indexedPreviewPending_ = false;
            if (windowed()) {
                requestIndexedPreview();
            }
        }
    });
    indexThread_->start(QThread::LowPriority);
}

void MainWindow::applyIndexedPreviewResult(const PreviewIndexThread& thread)
{
    if (!thread.success()) {
        previewMetaLabel_->setText(QStringLiteral("预览失败 · %1").arg(thread.errorMessage().left(72)));
        return;
    }
    const auto& index = thread.index();

    QString markdown;
    markdown.reserve(64 * 1024);
    bool charTruncated = false;
    for (const auto& block : index.blocks) {
        QString piece;
        switch (block.type) {
        case mqt::core::MarkdownBlockType::Heading: {
            const int level = std::clamp<int>(block.headingLevel, 1, 6);
            piece = QString(level, QLatin1Char('#'));
            piece += QLatin1Char(' ');
            piece += QString::fromUtf8(block.text.data(), static_cast<int>(block.text.size()));
            piece += QLatin1Char('\n');
            break;
        }
        case mqt::core::MarkdownBlockType::CodeFence: {
            piece = QStringLiteral("```\n");
            piece += QString::fromUtf8(block.text.data(), static_cast<int>(block.text.size()));
            piece += QStringLiteral("\n```\n");
            break;
        }
        case mqt::core::MarkdownBlockType::Paragraph:
        default: {
            piece = QString::fromUtf8(block.text.data(), static_cast<int>(block.text.size()));
            piece += QStringLiteral("\n\n");
            break;
        }
        }
        if (markdown.size() + piece.size() > kMaxIndexedPreviewChars) {
            charTruncated = true;
            break;
        }
        markdown += piece;
    }

    QUrl baseUrl;
    if (!currentPath_.empty()) {
        QString basePath = toQString(currentPath_.parent_path());
        if (!basePath.endsWith(QLatin1Char('/'))) {
            basePath += QLatin1Char('/');
        }
        baseUrl = QUrl::fromLocalFile(basePath);
    }
    const auto renderResult = renderMarkdownDocument(*preview_->document(), markdown, baseUrl);
    if (!renderResult.success) {
        previewMetaLabel_->setText(QStringLiteral("预览渲染失败 · %1").arg(renderResult.errorMessage.left(72)));
        return;
    }
    preview_->verticalScrollBar()->setValue(0);

    QString meta = QStringLiteral("索引预览（已保存内容）· 块 %1 · 扫描 %2 / 共 %3")
        .arg(index.blocks.size())
        .arg(formatFileSize(index.bytesScanned))
        .arg(formatFileSize(currentInfo_.sizeBytes));
    if (index.truncated || charTruncated) {
        meta += QStringLiteral(" · 已截断");
    }
    previewMetaLabel_->setText(meta);
}

void MainWindow::ensureDiskSpace(const std::filesystem::path& path, std::uint64_t neededBytes) const
{
    constexpr std::uint64_t kMargin = 1ULL << 20; // temp copy coexists during atomic replace
    constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t required = neededBytes > (kMax - kMargin) / 2 ? kMax : neededBytes * 2 + kMargin;
    const auto available = mqt::core::availableDiskBytes(path);
    if (available < required) {
        throw std::runtime_error(
            "not enough free disk space to save safely: need about "
            + std::to_string(required / (1024ULL * 1024ULL))
            + " MiB, but only "
            + std::to_string(available / (1024ULL * 1024ULL))
            + " MiB available");
    }
}

} // namespace mqt::gui
