// M13 会话层测试：往返、后台装载、取消域与跨会话隔离
#include "gui/document_session.h"

#include <QApplication>
#include <QElapsedTimer>

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void waitForLoadFinished(mqt::gui::DocumentSession& session, bool* finished,
    bool* ok, int timeoutMs = 15000)
{
    QObject::connect(&session, &mqt::gui::DocumentSession::loadFinished,
        [&](bool success, const QString&) {
            *finished = true;
            *ok = success;
        });
    QElapsedTimer timer;
    timer.start();
    while (!*finished && timer.elapsed() < timeoutMs) {
        QApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

std::string editorContent(mqt::gui::DocumentSession& session)
{
    auto& editor = session.editor();
    const auto len = editor.send(SCI_GETTEXTLENGTH);
    std::string out(static_cast<std::size_t>(len) + 1, '\0');
    editor.send(SCI_GETTEXT, len + 1, reinterpret_cast<sptr_t>(out.data()));
    out.resize(static_cast<std::size_t>(len));
    return out;
}

void testSyncRoundTrip(const std::filesystem::path& root)
{
    const auto path = root / "session-roundtrip.md";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "session content";
    }

    mqt::gui::DocumentSession session(1);
    require(session.openSync(path), "openSync must succeed");
    require(editorContent(session) == "session content", "content loaded");
    require(!session.isDirty(), "fresh session must be clean");

    mqt::core::TextEdit edit;
    edit.start = 15; edit.end = 15; edit.newText = " v2";
    (void)session.backend().apply({edit}, session.backend().snapshot().version);
    require(session.isDirty(), "session dirty after edit");

    const auto target = root / "session-roundtrip-out.md";
    require(session.saveAs(target), "saveAs must succeed");
    require(!session.isDirty(), "session clean after save");

    std::ifstream input(target, std::ios::binary);
    std::string saved((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    require(saved == "session content v2", "saveAs must persist the edit");
}

void testCrossSessionIsolation(const std::filesystem::path& root)
{
    // 大文件给会话 A 后台装载；装载中途创建会话 B 并装载自己的文档
    const auto bigPath = root / "session-big.md";
    {
        std::ofstream output(bigPath, std::ios::binary | std::ios::trunc);
        for (int i = 0; i < 8; ++i) {
            output << std::string(1024 * 1024, 'A') << '\n';
        }
    }
    const auto smallPath = root / "session-small.md";
    {
        std::ofstream output(smallPath, std::ios::binary | std::ios::trunc);
        output << "B CONTENT";
    }

    mqt::gui::DocumentSession sessionA(1);
    bool aFinished = false, aOk = false;
    require(sessionA.openBackground(bigPath, 256 * 1024),
        "session A background load must start");

    mqt::gui::DocumentSession sessionB(2);
    require(sessionB.openSync(smallPath), "session B openSync must succeed");

    waitForLoadFinished(sessionA, &aFinished, &aOk);
    require(aFinished && aOk, "session A load must complete");

    // 各自缓冲互不污染
    require(editorContent(sessionA).size() == (8ULL << 20) + 8,
        "session A buffer must hold its own document");
    require(editorContent(sessionB) == "B CONTENT",
        "session B buffer must be untouched by A's late chunks");

    // 销毁 A 后 B 完全不受影响
    sessionA.cancelLoad();
    require(editorContent(sessionB) == "B CONTENT",
        "session B unaffected after A teardown");
}

void testCancelThenReuse(const std::filesystem::path& root)
{
    const auto bigPath = root / "session-cancel-big.md";
    {
        std::ofstream output(bigPath, std::ios::binary | std::ios::trunc);
        for (int i = 0; i < 6; ++i) {
            output << std::string(1024 * 1024, 'C') << '\n';
        }
    }

    mqt::gui::DocumentSession session(3);
    bool finished = false, ok = true;
    require(session.openBackground(bigPath, 256 * 1024), "load must start");
    session.cancelLoad();
    waitForLoadFinished(session, &finished, &ok);
    require(finished && !ok, "cancelled load must report failure");

    // 同一会话可重新同步打开（取消域正确复位）
    const auto smallPath = root / "session-cancel-small.md";
    {
        std::ofstream output(smallPath, std::ios::binary | std::ios::trunc);
        output << "reuse ok";
    }
    require(session.openSync(smallPath), "session reusable after cancel");
    require(editorContent(session) == "reuse ok", "reopened content correct");
    require(!session.isDirty(), "reopened session clean");
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const auto root = std::filesystem::temp_directory_path() / "markdown_qt_document_session_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    try {
        testSyncRoundTrip(root);
        testCrossSessionIsolation(root);
        testCancelThenReuse(root);
        std::filesystem::remove_all(root);
        std::cout << "document session tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
