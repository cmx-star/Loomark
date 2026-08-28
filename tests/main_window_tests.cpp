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
        testStaleIndexCannotReplaceNormalPreview(window, root);
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
