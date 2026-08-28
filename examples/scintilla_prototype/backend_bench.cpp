// M07 等价后端对照基准
// 在同一大文件上对比 QPlainTextEdit（现状 Widgets 桥接）与 ScintillaEditBase
// 的打开、编辑、搜索、保存提取和峰值内存。样本不提交 Git。
//
// 两种后端分进程运行以保证 ru_maxrss 峰值互不污染：
//   markdown_qt_scintilla_bench <sample.md> widgets
//   markdown_qt_scintilla_bench <sample.md> scintilla
#include <QApplication>
#include <QPlainTextEdit>
#include <QFile>
#include <QElapsedTimer>

#include <ScintillaEditBase.h>
#include <ScintillaTypes.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/resource.h>

using namespace Scintilla;

namespace {

double msOf(const QElapsedTimer &t) {
    return double(t.nsecsElapsed()) / 1.0e6;
}

long peakRSSMB() {
    rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return long(ru.ru_maxrss) / (1024 * 1024); // macOS ru_maxrss 单位为字节
}

// 选取首现位置位于文件后 15% 的 ASCII 探针（要求含数字以提高唯一性），
// 保证“远端搜索”语义：搜索引擎需扫描到文件尾部才能命中
std::string pickFarNeedle(const QByteArray &data) {
    static constexpr int kMinRun = 12; // 样本中 “Section NNNNNN” 恰为 14 字符
    const qint64 minFirstOccurrence = data.size() * 85 / 100;
    const qint64 scanFrom = data.size() * 90 / 100;
    for (qint64 i = scanFrom; i + kMinRun < qint64(data.size()); ++i) {
        qint64 j = i;
        bool hasDigit = false;
        while (j < qint64(data.size()) &&
               ((data[int(j)] >= 'a' && data[int(j)] <= 'z') ||
                (data[int(j)] >= 'A' && data[int(j)] <= 'Z') ||
                (data[int(j)] >= '0' && data[int(j)] <= '9') ||
                data[int(j)] == ' ')) {
            if (data[int(j)] >= '0' && data[int(j)] <= '9') hasDigit = true;
            ++j;
        }
        if (j - i >= kMinRun && hasDigit) {
            // 取完整 run（截断前缀会提前命中小号 section 编码），上限 64 字符
            const qint64 runLen = qMin<qint64>(j - i, 64);
            std::string candidate(data.constData() + i, size_t(runLen));
            if (data.indexOf(candidate.c_str(), 0) >= minFirstOccurrence) {
                return candidate;
            }
        }
        i = j;
    }
    return {};
}

// 保存 = 字节流式写临时文件 + 原子替换（对齐 core 保存路径）
double writeAtomically(const char *path, const char *data, qint64 size,
                       const char *suffix) {
    QElapsedTimer t;
    t.start();
    const QString tmp = QString::fromLocal8Bit(path) + ".bench-tmp";
    QFile out(tmp);
    if (!out.open(QIODevice::WriteOnly)) {
        return -1.0;
    }
    const char *p = data;
    qint64 left = size;
    while (left > 0) {
        const qint64 chunk = qMin<qint64>(8 * 1024 * 1024, left);
        out.write(p, chunk);
        p += chunk;
        left -= chunk;
    }
    out.flush();
    ::fsync(out.handle());
    out.close();
    QFile::rename(tmp, QString::fromLocal8Bit(path) + QString::fromLocal8Bit(suffix));
    return msOf(t);
}

void runWidgets(const char *path, const QByteArray &bytes, const std::string &needle) {
    qInfo("--- QPlainTextEdit (current Widgets bridging) ---");
    QPlainTextEdit edit;
    QElapsedTimer t;

    t.start();
    edit.setPlainText(QString::fromUtf8(bytes));
    const double openMs = msOf(t);
    qInfo("open (setPlainText): %.1f ms, peak RSS %ld MB", openMs, peakRSSMB());

    const int mid = int(bytes.size() / 2);
    QTextCursor cur = edit.textCursor();
    cur.setPosition(mid);
    edit.setTextCursor(cur);
    t.start();
    cur.insertText(QString(512, 'x'));
    qInfo("edit (insert 512B at middle): %.1f ms", msOf(t));

    t.start();
    const bool found = edit.find(QString::fromStdString(needle));
    qInfo("search (find far needle, %d bytes): %.1f ms (found=%d)",
          int(needle.size()), msOf(t), int(found));

    t.start();
    const QString all = edit.toPlainText();
    const double extractMs = msOf(t);
    qInfo("extract (toPlainText, %lld MB): %.1f ms",
          qlonglong(all.size() / (1024 * 1024)), extractMs);

    const double saveMs = writeAtomically(path, all.toUtf8().constData(),
                                          all.size(), ".bench-widgets-out");
    qInfo("save (utf8 + stream write + rename): %.1f ms", saveMs);
    QFile::remove(QString::fromLocal8Bit(path) + ".bench-widgets-out");
    qInfo("final peak RSS: %ld MB", peakRSSMB());
}

void runScintilla(const char *path, const QByteArray &bytes, const std::string &needle) {
    qInfo("--- ScintillaEditBase (candidate backend) ---");
    ScintillaEditBase edit;
    edit.send(SCI_SETCODEPAGE, SC_CP_UTF8);
    edit.resize(600, 400);
    edit.show();
    QElapsedTimer t;

    t.start();
    edit.send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(const_cast<char *>(bytes.constData())));
    const double openMs = msOf(t);
    qInfo("open (SCI_SETTEXT): %.1f ms, peak RSS %ld MB", openMs, peakRSSMB());

    const sptr_t mid = sptr_t(bytes.size() / 2);
    const std::string insert(512, 'x');
    t.start();
    edit.send(SCI_INSERTTEXT, mid, reinterpret_cast<sptr_t>(const_cast<char *>(insert.c_str())));
    qInfo("edit (SCI_INSERTTEXT 512B at middle): %.1f ms", msOf(t));

    t.start();
    edit.send(SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE);
    edit.send(SCI_SETTARGETSTART, 0);
    edit.send(SCI_SETTARGETEND, edit.send(SCI_GETTEXTLENGTH));
    const sptr_t found = edit.send(SCI_SEARCHINTARGET, uptr_t(needle.size()),
                                   reinterpret_cast<sptr_t>(const_cast<char *>(needle.c_str())));
    qInfo("search (SCI_SEARCHINTARGET far needle, %d bytes): %.1f ms (found=%d at pos %ld)",
          int(needle.size()), msOf(t), int(found >= 0), long(found));

    // 分块提取写盘，不整体物化第二份完整字符串
    t.start();
    const QString tmp = QString::fromLocal8Bit(path) + ".bench-tmp";
    QFile out(tmp);
    if (!out.open(QIODevice::WriteOnly)) {
        fprintf(stderr, "cannot open temp file: %s\n", qPrintable(tmp));
        return;
    }
    std::string chunkBuf;
    constexpr sptr_t kWindow = 8 * 1024 * 1024;
    Sci_TextRangeFull range{};
    chunkBuf.resize(size_t(kWindow) + 1);
    const sptr_t docLen = edit.send(SCI_GETTEXTLENGTH);
    for (sptr_t start = 0; start < docLen;) {
        range.chrg.cpMin = start;
        range.chrg.cpMax = qMin(start + kWindow, docLen); // 防越界告警
        range.lpstrText = chunkBuf.data();
        const sptr_t got = edit.send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<sptr_t>(&range));
        if (got <= 0) break;
        out.write(chunkBuf.data(), got);
        start += got;
    }
    out.flush();
    ::fsync(out.handle());
    out.close();
    QFile::rename(tmp, QString::fromLocal8Bit(path) + ".bench-scintilla-out");
    qInfo("save (chunked SCI_GETTEXTRANGEFULL + write, no second full copy): %.1f ms", msOf(t));
    QFile::remove(QString::fromLocal8Bit(path) + ".bench-scintilla-out");
    qInfo("final peak RSS: %ld MB", peakRSSMB());
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <sample.md> [widgets|scintilla]\n", argv[0]);
        return 2;
    }
    QApplication app(argc, argv);
    const char *path = argv[1];
    const std::string backend = argv[2];

    QFile file(QString::fromLocal8Bit(path));
    QElapsedTimer t;
    t.start();
    if (!file.open(QIODevice::ReadOnly)) {
        fprintf(stderr, "cannot open sample: %s\n", path);
        return 2;
    }
    const QByteArray bytes = file.readAll();
    file.close();
    qInfo("sample: %s (%lld MB, raw read %.1f ms)", path,
          qlonglong(bytes.size() / (1024 * 1024)), msOf(t));

    const std::string needle = pickFarNeedle(bytes);
    if (needle.empty()) {
        fprintf(stderr, "no far needle found in sample\n");
        return 2;
    }
    qInfo("needle: \"%s\" (%d bytes, first occurrence in last 15%%)",
          needle.c_str(), int(needle.size()));

    if (backend == "widgets") {
        runWidgets(path, bytes, needle);
    } else if (backend == "scintilla") {
        runScintilla(path, bytes, needle);
    } else {
        fprintf(stderr, "unknown backend: %s\n", argv[2]);
        return 2;
    }
    return 0;
}
