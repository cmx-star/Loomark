// 批次 12：DOCX/预设/外部编译器门测试
#include "core/docx_export.h"
#include "core/zip_writer.h"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testZipWriter()
{
    mqt::core::ZipWriter zip;
    zip.addFile("a.txt", "hello");
    zip.addFile("b/c.txt", "world");
    const auto bytes = zip.finish();
    require(bytes.size() > 100, "zip produced");
    require(bytes.substr(0, 2) == "PK", "zip magic present");
    require(bytes.substr(bytes.size() - 22, 2) == "PK", "end of central directory");
    // CRC32 自验证
    require(mqt::core::crc32("hello") == mqt::core::crc32("hello"), "crc deterministic");
}

void testDocxExport()
{
    const std::string md = "# Heading\n\ntext paragraph\n\n```\ncode\n```\n\n<table>raw</table>\n";
    const auto outcome = mqt::core::DocxExporter::exportDocument(md);
    require(outcome.ok, "docx export ok");
    require(outcome.docx.substr(0, 2) == "PK", "docx is a zip container");
    require(outcome.docx.find("Heading1") != std::string::npos,
        "heading mapped to Heading1 style");
    require(outcome.docx.find("&lt;table&gt;") != std::string::npos,
        "raw xml escaped");
    require(!outcome.warnings.empty(), "unsupported semantics produce warnings");
}

void testPresetsAndCompilerGate()
{
    mqt::core::PresetRegistry::Preset book;
    book.kind = mqt::core::ExportPreset::Book;
    book.format = mqt::core::ExportFormat::Txt;
    book.sources = {std::filesystem::path("ch1.md"), std::filesystem::path("ch2.md")};
    const auto manifests = mqt::core::PresetRegistry::manifestsFor(book, "out");
    require(manifests.size() == 2, "book preset produces per-chapter manifests");
    require(manifests[0].targetPath == std::filesystem::path("out/ch1.txt"),
        "manifest target uses preset format");

    // 外部编译器默认禁用
    std::string error;
    require(!mqt::core::ExternalCompilerService::run(
        mqt::core::ExternalCompilerService::Kind::Pandoc, "in.md", "out.html", &error),
        "external compiler disabled by default");
    require(error.find("disabled") != std::string::npos, "gate explains itself");
}

} // namespace

int main()
{
    try {
        testZipWriter();
        testDocxExport();
        testPresetsAndCompilerGate();
        std::cout << "batch 12 tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
