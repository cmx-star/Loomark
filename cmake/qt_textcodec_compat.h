// qt_textcodec_compat.h — Qt6 兼容垫片：替代 Qt5 的 QTextCodec
// Qt6 从 Core 移除了 QTextCodec，Scintilla 的 Qt 平台层依赖它做非 UTF-8
// 编码转换。本垫片提供最小可用实现：所有转换都按 UTF-8 处理。
// 本项目主编辑器固定使用 UTF-8（SCI_SETCODEPAGE = SC_CP_UTF8），
// 因此该简化对原型验证无影响；遗留编码（GBK/Big5 等）不在 P0 范围内。
#ifndef MQT_QT_TEXTCODEC_COMPAT_H
#define MQT_QT_TEXTCODEC_COMPAT_H

#include <QByteArray>
#include <QString>

#include <cstring>
#include <string>

namespace QtCompat {

class QTextCodec {
public:
    static QTextCodec *codecForName(const QByteArray &name) {
        // 所有请求的编解码器统一按 UTF-8 语义处理（见文件头说明）
        static QTextCodec instance;
        return &instance;
    }
    static QTextCodec *codecForName(const char *name) {
        return codecForName(QByteArray(name ? name : ""));
    }

    QString toUnicode(const char *chars, int len) const {
        return QString::fromUtf8(chars, len);
    }
    QString toUnicode(const char *chars) const {
        return QString::fromUtf8(chars);
    }
    QString toUnicode(const QByteArray &ba) const {
        return QString::fromUtf8(ba);
    }

    QByteArray fromUnicode(const QString &s) const { return s.toUtf8(); }

    // ScintillaQt::CaseFolderDBCS 用 canEncode 判断折叠结果能否无损落回
    // 文档编码；垫片按 UTF-8 处理时 QString 恒可编码。
    bool canEncode(const QString &s) const {
        Q_UNUSED(s);
        return true;
    }
    bool canEncode(QChar c) const {
        Q_UNUSED(c);
        return true;
    }

private:
    QTextCodec() = default;
};

} // namespace QtCompat

#endif // MQT_QT_TEXTCODEC_COMPAT_H
