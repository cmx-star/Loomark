#include "gui/main_window.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QStringList>
#include <QStyleFactory>

#include <filesystem>
#include <string_view>

namespace {

QString loadStyleSheet()
{
    QFile file(QStringLiteral(":/qdarktheme/dark.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

std::filesystem::path toPath(const QString& text)
{
    const auto utf8 = text.toUtf8();
    return std::filesystem::path(std::u8string_view(
        reinterpret_cast<const char8_t*>(utf8.constData()),
        static_cast<std::size_t>(utf8.size())));
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setApplicationName(QStringLiteral("Loomark"));
    app.setOrganizationName(QStringLiteral("Loomark"));
    app.setWindowIcon(QIcon(QStringLiteral(":/brand/loomark.svg")));
    app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    app.setStyleSheet(loadStyleSheet());

    std::filesystem::path initialPath;
    const QStringList args = app.arguments();
    if (args.size() > 1) {
        initialPath = toPath(args.at(1));
    }

    mqt::gui::MainWindow window(initialPath);
    window.show();

    return app.exec();
}
