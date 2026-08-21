#pragma once

#include <QString>
#include <QUrl>

class QTextDocument;

namespace mqt::gui {

struct MarkdownRenderResult {
    bool success = false;
    QString errorMessage;
};

[[nodiscard]] MarkdownRenderResult renderMarkdownDocument(
    QTextDocument& target,
    const QString& markdown,
    const QUrl& baseUrl = {});

} // namespace mqt::gui
