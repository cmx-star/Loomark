#include "core/document_file.h"
#include "core/file_tier.h"
#include "gui/main_window.h"
#include "gui/preview_index_worker.h"
#include "gui/update_checker.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include "gui/document_session.h"
#include <QTabBar>
#include <QDockWidget>
#include <QSettings>
#include <QDir>
#include <QFileSystemModel>
#include <QTreeView>
#include <ScintillaEditBase.h>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextDocument>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mqt::gui {

class MainWindowTestAccess {
public:
    static bool configureWindowed(MainWindow& window, const std::filesystem::path& path)
    {
        window.largeMode_ = true;
        window.setCurrentPath(path, mqt::core::statFile(path));
        ++window.documentGeneration_;
        window.applyTierUiMode();
        if (!window.readWindow(0)) {
            return false;
        }
        window.launchInspectThread();
        window.dirty_ = false;
        return true;
    }

    static bool save(MainWindow& window, const std::filesystem::path& path)
    {
        return window.writeCurrentDocument(path);
    }

    static bool load(MainWindow& window, const std::filesystem::path& path)
    {
        return window.loadDocument(path);
    }

    static bool indexing(const MainWindow& window)
    {
        return window.indexThread_ != nullptr;
    }

    static std::filesystem::path currentPath(const MainWindow& window)
    {
        return window.currentPath_;
    }

    static bool windowed(const MainWindow& window)
    {
        return window.windowed();
    }

    static QString previewText(const MainWindow& window)
    {
        return window.preview_->toPlainText();
    }

    static QString previewMeta(const MainWindow& window)
    {
        return window.previewMetaLabel_->text();
    }

    static int editorCharacterCount(const MainWindow& window)
    {
        return std::max(0, window.editor_->document()->characterCount() - 1);
    }

    // M09: drive the visible Scintilla editor the way user typing would
    // (bypassing the backend apply() path).
    static void appendToEditor(MainWindow& window, std::string_view text)
    {
        window.activeSession_->editor().send(SCI_APPENDTEXT, text.size(),
            reinterpret_cast<Scintilla::sptr_t>(const_cast<char*>(text.data())));
    }

    // ---- M14 标签页 ----
    static int sessionCount(const MainWindow& window)
    {
        return window.sessions_.size();
    }
    static void activateSessionAt(MainWindow& window, int index)
    {
        window.activateSession(window.sessions_[index]);
    }
    static bool isSessionDirtyAt(const MainWindow& window, int index)
    {
        return window.sessions_[index]->isDirty();
    }
    static int sessionIndexForPath(const MainWindow& window,
        const std::filesystem::path& path)
    {
        for (int i = 0; i < window.sessions_.size(); ++i) {
            if (window.sessions_[i]->path() == path) {
                return i;
            }
        }
        return -1;
    }
    static std::string sessionEditorContent(const MainWindow& window, int index)
    {
        auto& editor = window.sessions_[index]->editor();
        const auto len = editor.send(SCI_GETTEXTLENGTH);
        std::string out(static_cast<std::size_t>(len) + 1, '\0');
        editor.send(SCI_GETTEXT, len + 1, reinterpret_cast<Scintilla::sptr_t>(out.data()));
        out.resize(static_cast<std::size_t>(len));
        return out;
    }
    static void appendToSessionAt(MainWindow& window, int index, std::string_view text)
    {
        window.sessions_[index]->editor().send(SCI_APPENDTEXT, text.size(),
            reinterpret_cast<Scintilla::sptr_t>(const_cast<char*>(text.data())));
    }
    static QString tabTextAt(const MainWindow& window, int index)
    {
        return window.tabBar_->tabText(index);
    }
    static bool closeSessionAt(MainWindow& window, int index)
    {
        return window.closeSession(window.sessions_[index]);
    }

    // ---- M15/M16 ----
    static bool isSaveEnabled(const MainWindow& window)
    {
        return window.saveAction_->isEnabled();
    }
    static bool isReloadEnabled(const MainWindow& window)
    {
        return window.reloadAction_->isEnabled();
    }
    static bool isFindEnabled(const MainWindow& window)
    {
        return window.findToggleAction_->isEnabled();
    }
    static QStringList recentFiles(const MainWindow& window)
    {
        return window.recentFiles_;
    }
    static void openWorkspaceDir(MainWindow& window, const QString& dir)
    {
        window.workspaceModel_->setRootPath(dir);
        window.workspaceTree_->setRootIndex(window.workspaceModel_->index(dir));
        window.workspaceDock_->show();
    }

    // ---- M17/M18 ----
    static QString recoveryDirOf(const MainWindow& window)
    {
        return window.recoveryDir();
    }
    static void clearRecoveryDir(const MainWindow& window)
    {
        QDir(window.recoveryDir()).removeRecursively();
    }
    static QStringList recoverySnapshots(const MainWindow& window)
    {
        QDir dir(window.recoveryDir());
        return dir.entryList(QStringList{QStringLiteral("session-*.md")}, QDir::Files);
    }
    static void clearSessionSettings()
    {
        QSettings settings;
        settings.clear();
    }
    static void saveSessionState(MainWindow& window)
    {
        window.saveSessionState();
    }

    static void shutdown(MainWindow& window)
    {
        window.shutdownBackgroundWork();
    }

    static void disableUpdateChecks(MainWindow& window)
    {
        delete window.updateChecker_;
        window.updateChecker_ = nullptr;
    }
};

} // namespace mqt::gui

namespace {

constexpr std::size_t kWindowBytes = 2 * 1024 * 1024;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeBinary(const std::filesystem::path& path, std::string_view content)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) {
        throw std::runtime_error("failed to create test file");
    }
}

std::string readWhole(const std::filesystem::path& path)
{
    const auto size = std::filesystem::file_size(path);
    return mqt::core::readRange(path, {0, size});
}

void drainBackground(mqt::gui::MainWindow& window)
{
    mqt::gui::MainWindowTestAccess::shutdown(window);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void testUnchangedWindowSavePreservesBytes(
    mqt::gui::MainWindow& window, const std::filesystem::path& root)
{
    const auto utf8Path = root / "utf8-boundary.md";
    std::string utf8Content(kWindowBytes - 3, 'a');
    utf8Content += std::string("\xE4\xB8\xAD", 3);
    utf8Content += "tail\n";
    writeBinary(utf8Path, utf8Content);

    require(mqt::gui::MainWindowTestAccess::configureWindowed(window, utf8Path),
        "UTF-8 boundary window must load");
    require(mqt::gui::MainWindowTestAccess::save(window, utf8Path),
        "UTF-8 boundary window must save");
    require(readWhole(utf8Path) == utf8Content,
        "unchanged save must preserve a complete UTF-8 character at the window end");
    drainBackground(window);

    const auto crlfPath = root / "crlf-boundary.md";
    std::string crlfContent(kWindowBytes - 1, 'b');
    crlfContent += "\r\nTAIL\r\n";
    writeBinary(crlfPath, crlfContent);

    require(mqt::gui::MainWindowTestAccess::configureWindowed(window, crlfPath),
        "CRLF boundary window must load");
    require(mqt::gui::MainWindowTestAccess::save(window, crlfPath),
        "CRLF boundary window must save");
    require(readWhole(crlfPath) == crlfContent,
        "unchanged save must preserve CRLF split at the nominal window boundary");
    drainBackground(window);
}

void answerNextQuestion(QMessageBox::StandardButton button)
{
    QTimer::singleShot(10, [button] {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* messageBox = qobject_cast<QMessageBox*>(widget)) {
                if (auto* response = messageBox->button(button)) {
                    response->click();
                }
                return;
            }
        }
    });
}

void dismissNextDialog(QMessageBox::StandardButton button, QString* capturedText = nullptr)
{
    QTimer::singleShot(10, [button, capturedText] {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* messageBox = qobject_cast<QMessageBox*>(widget)) {
                if (capturedText != nullptr) {
                    *capturedText = messageBox->text();
                }
                if (auto* response = messageBox->button(button)) {
                    response->click();
                }
                return;
            }
        }
    });
}

bool waitForIndex(mqt::gui::MainWindow& window, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (mqt::gui::MainWindowTestAccess::indexing(window) && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return !mqt::gui::MainWindowTestAccess::indexing(window);
}

void testNormalBackendSaveRoundTrip(
    mqt::gui::MainWindow& window, const std::filesystem::path& root)
{
    // M09: Normal tier round-trips through the Scintilla document backend;
    // BOM and mixed line endings must survive byte-for-byte.
    const auto srcPath = root / "backend-roundtrip.md";
    const std::string content = "\xEF\xBB\xBF# 标题\r\n\r\n正文 line\nlast\r\n";
    writeBinary(srcPath, content);

    require(mqt::gui::MainWindowTestAccess::load(window, srcPath),
        "normal document must open through the backend");
    require(!mqt::gui::MainWindowTestAccess::windowed(window),
        "normal document must not enter windowed mode");

    const auto target = root / "backend-roundtrip-out.md";
    require(mqt::gui::MainWindowTestAccess::save(window, target),
        "normal document must save through the backend");
    require(readWhole(target) == content,
        "backend save must round-trip BOM/CRLF bytes exactly");

    // Edit through the visible editor (user-typing path) and save again.
    mqt::gui::MainWindowTestAccess::appendToEditor(window, "\n## APPENDED");
    require(mqt::gui::MainWindowTestAccess::save(window, target),
        "edited document must save through the backend");
    const auto edited = readWhole(target);
    require(edited.rfind("## APPENDED") == edited.size() - 11,
        "user edit made through the editor must persist on save");

    drainBackground(window);
}

void writeTextFile(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

void testTabsDedupSwitchAndClose(
    mqt::gui::MainWindow& window, const std::filesystem::path& root)
{
    const auto file1 = root / "tab-one.md";
    const auto file2 = root / "tab-two.md";
    writeTextFile(file1, "FILE ONE");
    writeTextFile(file2, "FILE TWO");

    const int baseline = mqt::gui::MainWindowTestAccess::sessionCount(window);
    require(mqt::gui::MainWindowTestAccess::load(window, file1),
        "tab one must open");
    require(mqt::gui::MainWindowTestAccess::sessionCount(window) == baseline + 1,
        "one session added after first open");
    require(mqt::gui::MainWindowTestAccess::load(window, file2),
        "tab two must open");
    require(mqt::gui::MainWindowTestAccess::sessionCount(window) == baseline + 2,
        "two sessions added after second open");

    // 同路径去重
    require(mqt::gui::MainWindowTestAccess::load(window, file1),
        "reopening must succeed");
    require(mqt::gui::MainWindowTestAccess::sessionCount(window) == baseline + 2,
        "same path must not open a duplicate session");

    const int idx1 = mqt::gui::MainWindowTestAccess::sessionIndexForPath(window, file1);
    const int idx2 = mqt::gui::MainWindowTestAccess::sessionIndexForPath(window, file2);
    require(idx1 >= 0 && idx2 >= 0, "both sessions must be findable by path");

    // 切换后各会话内容保持正确（不复制正文的结构性验证）
    mqt::gui::MainWindowTestAccess::activateSessionAt(window, idx2);
    require(mqt::gui::MainWindowTestAccess::sessionEditorContent(window, idx2) == "FILE TWO",
        "session 2 content");
    mqt::gui::MainWindowTestAccess::activateSessionAt(window, idx1);
    require(mqt::gui::MainWindowTestAccess::sessionEditorContent(window, idx1) == "FILE ONE",
        "session 1 content preserved across switch");

    // 未保存标记
    mqt::gui::MainWindowTestAccess::appendToSessionAt(window, idx1, " edited");
    require(mqt::gui::MainWindowTestAccess::isSessionDirtyAt(window, idx1),
        "edited session must be dirty");
    require(mqt::gui::MainWindowTestAccess::tabTextAt(window, idx1).startsWith(
                QStringLiteral("•")),
        "dirty tab must show the bullet marker");

    // 关闭脏标签 → Discard
    answerNextQuestion(QMessageBox::Discard);
    require(mqt::gui::MainWindowTestAccess::closeSessionAt(window, idx1),
        "closing a dirty tab with discard must succeed");
    require(mqt::gui::MainWindowTestAccess::sessionCount(window) == baseline + 1,
        "one session remains");
    drainBackground(window);
}

void testCommandMatrixAndRecents(
    mqt::gui::MainWindow& window, const std::filesystem::path& root)
{
    // M16 启停矩阵：独立窗口从空态开始验证
    {
        mqt::gui::MainWindow fresh;
        mqt::gui::MainWindowTestAccess::disableUpdateChecks(fresh);
        require(!mqt::gui::MainWindowTestAccess::isSaveEnabled(fresh),
            "fresh window must disable save");
        require(!mqt::gui::MainWindowTestAccess::isReloadEnabled(fresh),
            "fresh window must disable reload");
        require(!mqt::gui::MainWindowTestAccess::isFindEnabled(fresh),
            "fresh window must disable find");

        const auto path = root / "cmd-matrix.md";
        writeTextFile(path, "command matrix");
        require(mqt::gui::MainWindowTestAccess::load(fresh, path),
            "open for command matrix");
        require(mqt::gui::MainWindowTestAccess::isReloadEnabled(fresh),
            "reload enabled with an open document");
        require(mqt::gui::MainWindowTestAccess::isFindEnabled(fresh),
            "find enabled for normal tier session");
        require(!mqt::gui::MainWindowTestAccess::isSaveEnabled(fresh),
            "save disabled while clean");
        mqt::gui::MainWindowTestAccess::appendToSessionAt(fresh, 0, " dirty");
        require(mqt::gui::MainWindowTestAccess::isSaveEnabled(fresh),
            "save enabled while dirty");
    }

    // M15 最近文件：去重 + 最新在前
    const auto rec1 = root / "recent-one.md";
    const auto rec2 = root / "recent-two.md";
    writeTextFile(rec1, "r1");
    writeTextFile(rec2, "r2");
    require(mqt::gui::MainWindowTestAccess::load(window, rec1), "open recent 1");
    require(mqt::gui::MainWindowTestAccess::load(window, rec2), "open recent 2");
    require(mqt::gui::MainWindowTestAccess::load(window, rec1), "reopen recent 1");
    const auto recents = mqt::gui::MainWindowTestAccess::recentFiles(window);
    require(recents.size() >= 2, "recent files recorded");
    // 去重路径不重复记录：最新「新打开」的是 rec2
    require(recents.front() == QString::fromStdString(rec2.string()),
        "most recent file first");
    require(recents.count(QString::fromStdString(rec1.string())) == 1,
        "recent files must be deduplicated");

    // M15 工作区：设置根目录
    mqt::gui::MainWindowTestAccess::openWorkspaceDir(window, QString::fromStdString(root.string()));
    drainBackground(window);
}

void testSessionPersistenceAndRecovery(
    mqt::gui::MainWindow& window, const std::filesystem::path& root)
{
    Q_UNUSED(window);
    QSettings settings;
    settings.clear();

    // 清理历史运行残留的恢复快照，保证断言只针对本次用例
    mqt::gui::MainWindowTestAccess::clearRecoveryDir(window);

    const auto path = root / "persist.md";
    writeTextFile(path, "PERSIST CONTENT");

    {
        mqt::gui::MainWindow w2;
        mqt::gui::MainWindowTestAccess::disableUpdateChecks(w2);
        require(mqt::gui::MainWindowTestAccess::load(w2, path),
            "persistence setup open");
        const int idx = mqt::gui::MainWindowTestAccess::sessionIndexForPath(w2, path);
        require(idx >= 0, "session opened for persistence test");

        // M18：脏文档 2s 后生成恢复快照
        mqt::gui::MainWindowTestAccess::appendToSessionAt(w2, idx, " dirty tail");
        QElapsedTimer timer;
        timer.start();
        while (mqt::gui::MainWindowTestAccess::recoverySnapshots(w2).isEmpty() &&
            timer.elapsed() < 4000) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        if (mqt::gui::MainWindowTestAccess::recoverySnapshots(w2).isEmpty()) {
            std::cerr << "DBG recoveryDir=" 
                      << mqt::gui::MainWindowTestAccess::recoveryDirOf(w2).toStdString()
                      << " dirty=" << mqt::gui::MainWindowTestAccess::isSessionDirtyAt(w2, idx)
                      << "\n";
        }
        require(!mqt::gui::MainWindowTestAccess::recoverySnapshots(w2).isEmpty(),
            "dirty document must produce a recovery snapshot");

        // 保存后快照清除
        require(mqt::gui::MainWindowTestAccess::save(w2, path),
            "save must succeed");
        QDir recDir(mqt::gui::MainWindowTestAccess::recoveryDirOf(w2));
        qDebug() << "DBG after save dir:" << recDir.entryList(QDir::Files);
        require(mqt::gui::MainWindowTestAccess::recoverySnapshots(w2).isEmpty(),
            "save must clear the recovery snapshot");

        // M17：持久化会话状态（直接调用，不依赖 close 事件语义）
        mqt::gui::MainWindowTestAccess::saveSessionState(w2);
    }

    // 新窗口恢复标签
    mqt::gui::MainWindow w3;
    mqt::gui::MainWindowTestAccess::disableUpdateChecks(w3);
    QApplication::processEvents(QEventLoop::AllEvents, 100);
    require(mqt::gui::MainWindowTestAccess::sessionCount(w3) >= 1,
        "restored window must reopen persisted tabs");
    const int idx = mqt::gui::MainWindowTestAccess::sessionIndexForPath(w3, path);
    require(idx >= 0, "persisted tab must be restored");
    require(mqt::gui::MainWindowTestAccess::sessionEditorContent(w3, idx) ==
            "PERSIST CONTENT dirty tail",
        "restored tab content must match");
}

void testStaleIndexCannotReplaceNormalPreview(
    mqt::gui::MainWindow& window, const std::filesystem::path& root)
{
    const auto largePath = root / "stale-large.md";
    {
        std::ofstream output(largePath, std::ios::binary | std::ios::trunc);
        output << "# OLD_INDEX_MARKER\n";
        output.seekp(static_cast<std::streamoff>(mqt::core::kNormalFileLimit), std::ios::beg);
        output.put('\n');
    }
    const auto normalPath = root / "current-normal.md";
    writeBinary(normalPath, "# NORMAL_CURRENT_MARKER\n");

    // 前置自建：确保处于 windowed 模式（不再依赖前序测试状态）
    if (!mqt::gui::MainWindowTestAccess::windowed(window)) {
        require(mqt::gui::MainWindowTestAccess::configureWindowed(window, largePath),
            "setup must enter windowed mode");
        drainBackground(window);
    }
    const auto previousPath = mqt::gui::MainWindowTestAccess::currentPath(window);
    answerNextQuestion(QMessageBox::No);
    require(!mqt::gui::MainWindowTestAccess::load(window, largePath),
        "declined large document must not open");
    require(mqt::gui::MainWindowTestAccess::currentPath(window) == previousPath,
        "declining a large document must preserve the current document");
    require(mqt::gui::MainWindowTestAccess::windowed(window),
        "declining a large document must preserve the current large-file mode");

    dismissNextDialog(QMessageBox::Ok);
    require(!mqt::gui::MainWindowTestAccess::load(window, root / "missing.md"),
        "missing document must fail to open");
    require(mqt::gui::MainWindowTestAccess::currentPath(window) == previousPath,
        "failed metadata inspection must preserve the current document");
    require(mqt::gui::MainWindowTestAccess::windowed(window),
        "failed metadata inspection must preserve the current large-file mode");

    answerNextQuestion(QMessageBox::Yes);
    require(mqt::gui::MainWindowTestAccess::load(window, largePath),
        "large document must open for stale-index regression");
    require(mqt::gui::MainWindowTestAccess::indexing(window),
        "large document must start an index request");
    require(mqt::gui::MainWindowTestAccess::load(window, normalPath),
        "normal document must replace the large document");

    require(waitForIndex(window, 5000),
        "cancelled stale index must finish promptly");
    require(mqt::gui::MainWindowTestAccess::currentPath(window) == normalPath,
        "normal document must remain current");
    const auto preview = mqt::gui::MainWindowTestAccess::previewText(window);
    require(preview.contains(QStringLiteral("NORMAL_CURRENT_MARKER")),
        "normal document preview must remain visible");
    require(!preview.contains(QStringLiteral("OLD_INDEX_MARKER")),
        "stale large-document preview must be discarded");
    drainBackground(window);
}

bool filesEqual(const std::filesystem::path& left, const std::filesystem::path& right)
{
    std::error_code ec;
    const auto leftSize = std::filesystem::file_size(left, ec);
    if (ec) {
        return false;
    }
    const auto rightSize = std::filesystem::file_size(right, ec);
    if (ec || leftSize != rightSize) {
        return false;
    }

    std::ifstream leftInput(left, std::ios::binary);
    std::ifstream rightInput(right, std::ios::binary);
    if (!leftInput || !rightInput) {
        return false;
    }

    constexpr std::size_t kCompareBytes = 1024 * 1024;
    std::vector<char> leftBuffer(kCompareBytes);
    std::vector<char> rightBuffer(kCompareBytes);
    while (leftInput && rightInput) {
        leftInput.read(leftBuffer.data(), static_cast<std::streamsize>(leftBuffer.size()));
        rightInput.read(rightBuffer.data(), static_cast<std::streamsize>(rightBuffer.size()));
        if (leftInput.gcount() != rightInput.gcount() ||
            !std::equal(leftBuffer.begin(), leftBuffer.begin() + leftInput.gcount(), rightBuffer.begin())) {
            return false;
        }
    }
    return leftInput.eof() && rightInput.eof();
}

int runProbe(const std::filesystem::path& source, const std::filesystem::path& saveTarget,
    bool expectSaveFailure)
{
    mqt::gui::MainWindow window;
    mqt::gui::MainWindowTestAccess::disableUpdateChecks(window);

    const auto info = mqt::core::statFile(source);
    if (info.tier != mqt::core::FileTier::Normal) {
        QTimer::singleShot(10, [tier = info.tier] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* messageBox = qobject_cast<QMessageBox*>(widget)) {
                    const auto button = tier == mqt::core::FileTier::Reject
                        ? QMessageBox::Ok
                        : QMessageBox::Yes;
                    if (auto* response = messageBox->button(button)) {
                        response->click();
                    }
                    return;
                }
            }
        });
    }

    QElapsedTimer timer;
    timer.start();
    const bool opened = mqt::gui::MainWindowTestAccess::load(window, source);
    const auto openMs = timer.elapsed();

    if (info.tier == mqt::core::FileTier::Reject) {
        require(!opened, "REJECT probe file must not open");
        std::cout << "tier=reject opened=false open_ms=" << openMs << "\n";
        return EXIT_SUCCESS;
    }
    require(opened, "probe file must open");

    timer.restart();
    require(waitForIndex(window, 15000), "probe index must complete");
    const auto indexMs = timer.elapsed();

    bool saved = false;
    bool equal = false;
    bool targetPreserved = false;
    qint64 saveMs = 0;
    QString saveError;
    if (!saveTarget.empty()) {
        std::string originalTarget;
        if (expectSaveFailure) {
            originalTarget = readWhole(saveTarget);
            dismissNextDialog(QMessageBox::Ok, &saveError);
        } else {
            std::filesystem::remove(saveTarget);
        }
        timer.restart();
        saved = mqt::gui::MainWindowTestAccess::save(window, saveTarget);
        saveMs = timer.elapsed();
        if (expectSaveFailure) {
            targetPreserved = !saved && readWhole(saveTarget) == originalTarget;
            require(targetPreserved, "failed probe save must preserve the existing target");
        } else {
            equal = saved && filesEqual(source, saveTarget);
            require(equal, "probe Save As output must match the unchanged source");
        }
    }

    std::cout << "tier=" << mqt::core::toString(info.tier)
              << " opened=true"
              << " open_ms=" << openMs
              << " index_ms=" << indexMs
              << " editor_chars=" << mqt::gui::MainWindowTestAccess::editorCharacterCount(window)
              << " preview_meta=\""
              << mqt::gui::MainWindowTestAccess::previewMeta(window).toStdString() << "\"";
    if (!saveTarget.empty()) {
        std::cout << " save_ms=" << saveMs;
        if (expectSaveFailure) {
            std::cout << " save_failed=" << (!saved ? "true" : "false")
                      << " target_preserved=" << (targetPreserved ? "true" : "false")
                      << " error=\"" << saveError.toStdString() << "\"";
        } else {
            std::cout << " save_equal=" << (equal ? "true" : "false");
        }
    }
    std::cout << "\n";
    drainBackground(window);
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    const bool probe = argc >= 3 && std::string_view(argv[1]) == "--probe";
    const bool failureProbe = argc >= 4 && std::string_view(argv[1]) == "--probe-save-failure";
    if (probe || failureProbe) {
        try {
            const std::filesystem::path saveTarget = argc >= 4
                ? std::filesystem::path(argv[3])
                : std::filesystem::path();
            return runProbe(std::filesystem::path(argv[2]), saveTarget, failureProbe);
        } catch (const std::exception& error) {
            std::cerr << "probe failure: " << error.what() << "\n";
            return EXIT_FAILURE;
        }
    }

    const auto root = std::filesystem::temp_directory_path() / "markdown_qt_main_window_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    try {
        mqt::gui::MainWindow window;
        mqt::gui::MainWindowTestAccess::disableUpdateChecks(window);
        testUnchangedWindowSavePreservesBytes(window, root);
        testSessionPersistenceAndRecovery(window, root);
        testCommandMatrixAndRecents(window, root);
        testStaleIndexCannotReplaceNormalPreview(window, root);
        testTabsDedupSwitchAndClose(window, root);
        testNormalBackendSaveRoundTrip(window, root);
        std::filesystem::remove_all(root);
        std::cout << "main window tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root);
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
