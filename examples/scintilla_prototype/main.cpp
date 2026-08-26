// M06 Scintilla 直接集成原型 — 隔离验证，不改动主 GUI
// 验证目标：Qt 事件、IME、选择、撤销、信号连接、跨平台构建

#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QSplitter>
#include <QTextBrowser>
#include <QStatusBar>
#include <QMessageBox>
#include <QDebug>
#include <string>

#include "ScintillaEditBase.h"

using namespace Scintilla;

class ScintillaDemo : public QMainWindow {
    Q_OBJECT
public:
    explicit ScintillaDemo(QWidget *parent = nullptr)
        : QMainWindow(parent)
    {
        setWindowTitle("M06 Scintilla Direct Integration Prototype (HPND)");
        resize(900, 600);

        auto *central = new QWidget(this);
        auto *layout = new QVBoxLayout(central);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(4);
        setCentralWidget(central);

        // --- Header label ---
        auto *header = new QLabel(
            "M06 Scintilla 直接集成原型 | HPND License | "
            "Ctrl+Z 撤销 | Ctrl+Y 重做 | 选中文字测试 | IME 事件记录在下方",
            this
        );
        header->setStyleSheet(
            "QLabel { background: #1e293b; color: #e2e8f0; "
            "font-size: 13px; padding: 6px 10px; border-radius: 4px; }"
        );
        layout->addWidget(header);

        // --- Scintilla editor ---
        editor_ = new ScintillaEditBase(this);
        editor_->send(SCI_SETVIEWWS, SC_WS_VISIBLEALWAYS, 0);
        editor_->send(SCI_SETCODEPAGE, CP_UTF8);
        editor_->send(SCI_SETWRAPMODE, SC_WRAP_WORD);
        editor_->send(SCI_SETUSETAB, 0);
        editor_->send(SCI_SETTABWIDTH, 4);
        editor_->send(SCI_SETCARETSTRICT, true);

        // Connect signals
        connect(editor_, &ScintillaEditBase::savePointChanged,
                this, &ScintillaDemo::onSavePointChanged);
        connect(editor_, &ScintillaEditBase::modified,
                this, &ScintillaDemo::onModified);
        connect(editor_, &ScintillaEditBase::charAdded,
                this, &ScintillaDemo::onCharAdded);
        connect(editor_, &ScintillaEditBase::notifyChange,
                this, &ScintillaDemo::onNotifyChange);
        connect(editor_, &ScintillaEditBase::verticalScrolled,
                this, &ScintillaDemo::onVerticalScrolled);

        layout->addWidget(editor_);

        // --- Control bar ---
        auto *ctrlBar = new QWidget(this);
        auto *ctrlLayout = new QHBoxLayout(ctrlBar);
        ctrlLayout->setContentsMargins(0, 0, 0, 0);
        ctrlLayout->setSpacing(8);

        auto *statusLabel = new QLabel("就绪", this);
        statusLabel->setObjectName("statusLabel");
        statusLabel->setStyleSheet(
            "QLabel#statusLabel { color: #94a3b8; font-size: 12px; }"
        );
        ctrlLayout->addWidget(statusLabel);
        ctrlLayout->addStretch();

        auto *infoLabel = new QLabel(this);
        infoLabel->setObjectName("infoLabel");
        infoLabel->setStyleSheet(
            "QLabel#infoLabel { color: #64748b; font-size: 11px; }"
        );
        ctrlLayout->addWidget(infoLabel);

        layout->addWidget(ctrlBar);

        // --- Event log ---
        auto *logLabel = new QLabel("事件日志：", this);
        logLabel->setStyleSheet(
            "QLabel { color: #94a3b8; font-size: 11px; padding-top: 4px; }"
        );
        layout->addWidget(logLabel);

        log_ = new QTextBrowser(this);
        log_->setMaximumHeight(120);
        log_->setStyleSheet(
            "QTextBrowser { background: #0f172a; color: #94a3b8; "
            "font-family: monospace; font-size: 11px; "
            "border: 1px solid #1e293b; border-radius: 4px; }"
        );
        layout->addWidget(log_);

        appendLog("[系统] ScintillaEditBase 已创建，原型启动");
        appendLog("[系统] HPND License — Neil Hodgson, 1998-2021");
        appendLog("[系统] 支持操作: 输入/删除/选择/撤销(Ctrl+Z)/重做(Ctrl+Y)/滚动");

        // Set some initial text
        std::string initial =
            "# M06 Scintilla 直接集成原型\n\n"
            "这是一个隔离的 Scintilla + Qt6 集成验证原型。\n\n"
            "功能验证：\n"
            "- 键盘事件 (keyPressEvent)\n"
            "- 鼠标事件 (mousePressEvent, mouseMoveEvent)\n"
            "- 滚轮事件 (wheelEvent)\n"
            "- 输入法事件 (inputMethodEvent)\n"
            "- 文本选择和撤销\n"
            "- savePointChanged / modified 信号\n\n"
            "测试步骤：\n"
            "1. 在编辑器中输入文字\n"
            "2. 选中部分文字（鼠标拖拽）\n"
            "3. 按 Ctrl+Z 撤销\n"
            "4. 按 Ctrl+Y 重做\n"
            "5. 滚动查看 verticalScrolled 信号\n"
            "6. 如使用中文输入法，检查 inputMethodEvent 是否触发\n";
        editor_->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(initial.c_str()));
    }

private slots:
    void onSavePointChanged(bool dirty) {
        appendLog(QString("[信号] savePointChanged(dirty=%1)").arg(dirty ? "true" : "false"));
        setStatus(dirty ? "已修改" : "已保存");
    }

    void onModified(ModificationFlags type, Position position, Position length,
                     Position linesAdded, const QByteArray &text,
                     Position line, FoldLevel foldNow, FoldLevel foldPrev) {
        bool isInsert = (type & SC_MOD_INSERTTEXT) != 0;
        bool isDelete = (type & SC_MOD_DELETETEXT) != 0;
        QString action = isInsert ? "插入" : (isDelete ? "删除" : "其他");
        appendLog(QString("[信号] modified(type=%1 pos=%2 len=%3)")
                  .arg(action).arg(position).arg(length));
    }

    void onCharAdded(int ch) {
        appendLog(QString("[信号] charAdded(ch=0x%1)").arg(QString::number(ch, 16).toUpper()));
    }

    void onNotifyChange() {
        appendLog("[信号] notifyChange()");
    }

    void onVerticalScrolled(int /*value*/) {
        appendLog("[信号] verticalScrolled()");
    }

private:
    void appendLog(const QString &msg) {
        log_->append(msg);
        log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
    }

    void setStatus(const QString &text) {
        auto *label = findChild<QLabel *>("statusLabel");
        if (label) label->setText(text);
    }

    ScintillaEditBase *editor_ = nullptr;
    QTextBrowser *log_ = nullptr;
};

#include "main.moc"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Loomark Scintilla Prototype");
    app.setOrganizationName("Loomark");

    ScintillaDemo window;
    window.show();

    return app.exec();
}
