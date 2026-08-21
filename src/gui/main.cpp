#include "gui/main_window.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QStyleFactory>

namespace {

QString loadStyleSheet()
{
    QFile file(QStringLiteral(":/qdarktheme/dark.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setApplicationName(QStringLiteral("Markdown Qt"));
    app.setOrganizationName(QStringLiteral("markdown-qt"));
    app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    app.setStyleSheet(loadStyleSheet());

    mqt::gui::MainWindow window;
    window.show();

    return app.exec();
}
