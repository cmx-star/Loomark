#include "core/pdf_export.h"

#include <QFont>
#include <QPdfWriter>
#include <QTextDocument>

namespace mqt::core {

bool PdfExporter::exportPdf(const QString& sourceText, const QString& targetPath,
    std::string* error, std::uint64_t maxSourceBytes)
{
    const auto bytes = static_cast<std::uint64_t>(sourceText.toUtf8().size());
    if (bytes > maxSourceBytes) {
        if (error != nullptr) {
            *error = "PDF export is tier-limited (source too large)";
        }
        return false;
    }
    QPdfWriter writer(targetPath);
    writer.setPageSize(QPagedPaintDevice::A4);
    writer.setResolution(150);
    QTextDocument doc;
    doc.setDefaultFont(QFont("Helvetica", 10));
    doc.setPlainText(sourceText);
    doc.print(&writer);
    return true;
}

} // namespace mqt::core
