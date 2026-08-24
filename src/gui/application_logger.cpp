#include "gui/application_logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>

#include <cstdio>

namespace mqt::gui {
namespace {

QMutex logMutex;
QFile* logFile = nullptr;
QtMessageHandler previousMessageHandler = nullptr;
QString currentLogPath;
QString currentLogDir;

QString messageTypeName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("LOG");
}

QString writableLogDirectory()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath() + QStringLiteral("/Loomark");
    }
    return QDir(base).filePath(QStringLiteral("logs"));
}

void writeMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QString line = QStringLiteral("%1 [%2] %3%4\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
        .arg(messageTypeName(type))
        .arg(message)
        .arg(context.file
            ? QStringLiteral(" (%1:%2)").arg(QString::fromUtf8(context.file)).arg(context.line)
            : QString());

    {
        QMutexLocker locker(&logMutex);
        if (logFile && logFile->isOpen()) {
            QTextStream stream(logFile);
            stream << line;
            stream.flush();
            logFile->flush();
        }
    }

    std::fputs(line.toUtf8().constData(), stderr);
    std::fflush(stderr);

    if (previousMessageHandler) {
        previousMessageHandler(type, context, message);
    }
}

} // namespace

QString initializeApplicationLogging()
{
    QMutexLocker locker(&logMutex);
    if (logFile && logFile->isOpen()) {
        return currentLogPath;
    }

    currentLogDir = writableLogDirectory();
    QDir().mkpath(currentLogDir);
    currentLogPath = QDir(currentLogDir).filePath(QStringLiteral("loomark.log"));

    auto* file = new QFile(currentLogPath);
    if (file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        logFile = file;
        previousMessageHandler = qInstallMessageHandler(writeMessage);
        QTextStream stream(logFile);
        stream << "\n"
               << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
               << " [INFO] Loomark logging started\n";
        stream.flush();
        logFile->flush();
    } else {
        delete file;
    }

    return currentLogPath;
}

QString applicationLogFilePath()
{
    return currentLogPath;
}

QString applicationLogDirectory()
{
    return currentLogDir;
}

} // namespace mqt::gui
