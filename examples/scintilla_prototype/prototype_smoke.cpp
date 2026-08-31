// M06 Scintilla 原型自动化冒烟测试
// 覆盖 PRD 验收条件：文本往返、键入、撤销/重做、选择、IME 事件、信号触发。
// 运行于 QT_QPA_PLATFORM=offscreen，不依赖显示器。
#include <QApplication>
#include <QInputMethodEvent>
#include <QSignalSpy>

#include <QtTest/QTest>

#include <ScintillaEditBase.h>
#include <ScintillaTypes.h>

using namespace Scintilla;

namespace {

bool g_inputMethodSeen = false;

class ProbeEditor : public ScintillaEditBase {
public:
    using ScintillaEditBase::ScintillaEditBase;

protected:
    void inputMethodEvent(QInputMethodEvent *event) override {
        g_inputMethodSeen = true;
        ScintillaEditBase::inputMethodEvent(event);
    }
};

std::string textOf(ProbeEditor &edit) {
    auto len = edit.send(SCI_GETTEXTLENGTH);
    std::string out(static_cast<size_t>(len) + 1, '\0');
    edit.send(SCI_GETTEXT, len + 1, reinterpret_cast<sptr_t>(out.data()));
    out.resize(static_cast<size_t>(len));
    return out;
}

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    ProbeEditor edit;
    edit.send(SCI_SETCODEPAGE, SC_CP_UTF8);
    edit.resize(600, 400);
    edit.show();
    if (!QTest::qWaitForWindowExposed(&edit)) {
        return 2;
    }

    int failures = 0;
    auto check = [&](bool ok, const char *what) {
        qInfo("%s %s", ok ? "[PASS]" : "[FAIL]", what);
        if (!ok) ++failures;
    };

    // 1. 文本设置/读取往返
    const std::string initial = "line one\nline two\n";
    edit.send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(initial.c_str()));
    check(textOf(edit) == initial, "SCI_SETTEXT/SCI_GETTEXT roundtrip");

    // 2. 键入
    edit.send(SCI_GOTOPOS, static_cast<uptr_t>(initial.size()));
    QTest::keyClicks(&edit, "abc");
    check(textOf(edit) == initial + "abc", "keyboard typing inserts text");

    // 3. 撤销（Ctrl+Z）
    QTest::keyClick(&edit, Qt::Key_Z, Qt::ControlModifier);
    check(textOf(edit) == initial, "Ctrl+Z undoes typed text");

    // 4. 重做（Ctrl+Y）
    QTest::keyClick(&edit, Qt::Key_Y, Qt::ControlModifier);
    check(textOf(edit) == initial + "abc", "Ctrl+Y redoes typed text");

    // 5. 选择
    edit.send(SCI_SELECTALL);
    auto selStart = edit.send(SCI_GETSELECTIONSTART);
    auto selEnd = edit.send(SCI_GETSELECTIONEND);
    check(selStart == 0 && selEnd == static_cast<sptr_t>(initial.size() + 3),
          "SCI_SELECTALL selects whole buffer");

    // 6. IME 事件接收（inputMethodEvent 被调用并提交文本）
    g_inputMethodSeen = false;
    edit.send(SCI_GOTOPOS, static_cast<uptr_t>(initial.size() + 3));
    QInputMethodEvent ime;
    ime.setCommitString("中文");
    QApplication::sendEvent(&edit, &ime);
    check(g_inputMethodSeen, "inputMethodEvent override invoked");
    check(textOf(edit) == initial + "abc中文", "IME commit string lands in buffer");

    // 7. 信号触发（先把保存点归零，再验证修改引发的 dirty 切换）
    edit.send(SCI_SETSAVEPOINT);
    QTest::qWait(0);
    QSignalSpy modifiedSpy(&edit, &ScintillaEditBase::modified);
    QSignalSpy savePointSpy(&edit, &ScintillaEditBase::savePointChanged);
    edit.send(SCI_INSERTTEXT, 0, reinterpret_cast<sptr_t>("# "));
    check(modifiedSpy.count() > 0, "modified signal emitted on insert");
    check(savePointSpy.count() > 0, "savePointChanged signal emitted");

    // 8. 撤销历史可用
    edit.send(SCI_UNDO);
    check(textOf(edit).rfind(initial + "abc中文", 0) == 0,
          "undo removes programmatic insert");

    qInfo("%s", failures == 0 ? "SMOKE RESULT: ALL PASS"
                              : "SMOKE RESULT: FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
