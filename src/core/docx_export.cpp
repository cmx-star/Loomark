#include "core/docx_export.h"
#include "core/zip_writer.h"

#include <fstream>
#include <sstream>

namespace mqt::core {

namespace {

std::string escapeXml(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '&': out += "&amp;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

std::string paragraph(const std::string& text, const char* style = nullptr)
{
    std::string xml = "<w:p>";
    if (style != nullptr) {
        xml += "<w:pPr><w:pStyle w:val=\"";
        xml += style;
        xml += "\"/></w:pPr>";
    }
    xml += "<w:r><w:t xml:space=\"preserve\">";
    xml += escapeXml(text);
    xml += "</w:t></w:r></w:p>";
    return xml;
}

} // namespace

DocxExporter::Outcome DocxExporter::exportDocument(std::string_view markdown)
{
    Outcome outcome;
    // 块级解析复用统一块模型
    MarkdownBlockIndex index;
    index.build(markdown, 1);

    std::string body;
    for (const auto& block : index.blocks()) {
        std::string text = block.payload;
        switch (block.kind) {
        case BlockKind::Heading: {
            std::size_t i = 0;
            while (i < text.size() && text[i] == '#') ++i;
            while (i < text.size() && text[i] == ' ') ++i;
            const char* style = block.level == 1 ? "Heading1"
                : block.level == 2 ? "Heading2" : "Heading3";
            body += paragraph(text.substr(i), style);
            break;
        }
        case BlockKind::ListItem:
            body += paragraph(text.substr(text.find_first_not_of("-*+0123456789. ")));
            break;
        case BlockKind::CodeFence:
            body += paragraph(text, "Code");
            break;
        case BlockKind::Paragraph:
        case BlockKind::Quote:
            body += paragraph(text);
            break;
        case BlockKind::Table:
        case BlockKind::Html:
        case BlockKind::HorizontalRule:
            outcome.warnings.push_back("block kind " +
                std::to_string(static_cast<int>(block.kind)) + " at offset " +
                std::to_string(block.sourceRange.start) + " kept as plain text");
            body += paragraph(text);
            break;
        }
    }

    const std::string documentXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:body>" + body +
        "<w:sectPr/></w:body></w:document>";

    ZipWriter zip;
    zip.addFile("[Content_Types].xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "</Types>");
    zip.addFile("_rels/.rels",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>");
    zip.addFile("word/document.xml", documentXml);
    outcome.docx = zip.finish();
    outcome.ok = true;
    return outcome;
}

std::vector<ExportManifest> PresetRegistry::manifestsFor(const Preset& preset,
    const std::filesystem::path& outputDir)
{
    std::vector<ExportManifest> manifests;
    for (const auto& source : preset.sources) {
        ExportManifest m;
        m.sourcePath = source;
        m.targetPath = outputDir / (source.stem().string() +
            (preset.format == ExportFormat::Txt ? ".txt"
             : preset.format == ExportFormat::SafeHtml ? ".html" : ".md"));
        m.format = preset.format;
        manifests.push_back(std::move(m));
    }
    return manifests;
}

bool ExternalCompilerService::detect(Kind kind)
{
    const char* binary = kind == Kind::Pandoc ? "pandoc"
        : kind == Kind::Typst ? "typst" : "quarkdown";
    const std::string cmd = std::string(binary) + " --version >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

bool ExternalCompilerService::run(Kind kind, const std::filesystem::path& input,
    const std::filesystem::path& output, std::string* error)
{
    if (!enabled_) {
        if (error != nullptr) {
            *error = "external compilers are disabled by default";
        }
        return false;
    }
    const char* binary = kind == Kind::Pandoc ? "pandoc"
        : kind == Kind::Typst ? "typst" : "quarkdown";
    std::string cmd = std::string(binary) + " '" + input.string() + "' -o '" +
        output.string() + "' 2>&1";
    if (std::system(cmd.c_str()) == 0) {
        return true;
    }
    if (error != nullptr) {
        *error = "external compiler failed";
    }
    return false;
}

} // namespace mqt::core
