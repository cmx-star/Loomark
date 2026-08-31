// 批次 11：导出基础测试
#include "core/export.h"

#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string readWhole(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
}

void testFrontMatter()
{
    const std::string content = "---\ntitle: my doc\nunknown_field: keep me\n---\nbody\n";
    const auto fm = mqt::core::parseFrontMatter(content);
    require(fm.present, "front matter detected");
    require(fm.rawYaml.find("unknown_field: keep me") != std::string::npos,
        "unknown fields preserved verbatim");
    require(fm.bodyOffset == content.size() - 5, "body offset after closing fence");
    require(!mqt::core::parseFrontMatter("no fence").present,
        "no front matter when absent");
}

void testGfmExport()
{
    const auto root = std::filesystem::temp_directory_path() / "mqt_export_test";
    std::filesystem::create_directories(root);
    const auto src = root / "doc.md";
    const auto target = root / "doc-gfm.md";
    std::filesystem::remove(target);
    {
        std::ofstream output(src, std::ios::binary | std::ios::trunc);
        output << "# Title\r\n\r\nbody\r\n";
    }

    mqt::core::ExportTask task({src, target, mqt::core::ExportFormat::Gfm, {}, 1 << 20});
    const auto written = task.run();
    require(written > 0, "gfm export wrote bytes");
    const auto out = readWhole(target);
    require(out.find("# Title") != std::string::npos, "gfm keeps markers");
    require(out.find("\r") == std::string::npos, "gfm normalizes CRLF to LF");
}

void testTxtExport()
{
    const auto root = std::filesystem::temp_directory_path() / "mqt_export_test";
    const auto src = root / "doc.md";
    const auto target = root / "doc.txt";
    std::filesystem::remove(target);

    mqt::core::ExportTask task({src, target, mqt::core::ExportFormat::Txt, {}, 1 << 20});
    (void)task.run();
    const auto out = readWhole(target);
    require(out.find("Title") != std::string::npos && out.find("# Title") == std::string::npos,
        "txt strips heading markers");
    require(out.find("```") == std::string::npos, "txt strips fence markers");
    require(out.find("body") != std::string::npos, "txt keeps body");
}

void testSafeHtml()
{
    const auto root = std::filesystem::temp_directory_path() / "mqt_export_test";
    const auto src = root / "evil.md";
    const auto target = root / "evil.html";
    std::filesystem::remove(target);
    {
        std::ofstream output(src, std::ios::binary | std::ios::trunc);
        output << "# Hi<script>alert(1)</script>\n<a href=\"http://x\">y</a>\n";
    }

    mqt::core::ExportTask task({src, target, mqt::core::ExportFormat::SafeHtml, {}, 1 << 20});
    (void)task.run();
    const auto out = readWhole(target);
    require(out.find("<script>") == std::string::npos,
        "raw script tags must be escaped");
    require(out.find("&lt;script&gt;") != std::string::npos, "script escaped");
    require(out.find("<!DOCTYPE html>") != std::string::npos, "html wrapper present");
}

void testCancelAndFailureSafety()
{
    const auto root = std::filesystem::temp_directory_path() / "mqt_export_test";
    const auto src = root / "big.md";
    const auto target = root / "big-gfm.md";
    {
        std::ofstream output(src, std::ios::binary | std::ios::trunc);
        for (int i = 0; i < 40; ++i) {
            output << std::string(1024 * 1024, 'x') << "\n";
        }
    }
    std::filesystem::remove(target);

    // 取消：目标不应被创建
    std::atomic_bool cancel{true};
    mqt::core::ExportTask task({src, target, mqt::core::ExportFormat::Gfm, {}, 1 << 30});
    const auto result = task.run(&cancel);
    require(result == -1, "cancelled export returns -1");
    require(!std::filesystem::exists(target), "cancelled export leaves no target");

    // 资源预算：超预算抛异常
    bool threw = false;
    try {
        mqt::core::ExportTask over({src, target, mqt::core::ExportFormat::Gfm, {}, 1024});
        (void)over.run();
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, "over-budget export throws");
    require(!std::filesystem::exists(target), "failed export leaves no target");

    // 选定章节
    const auto sectionTarget = root / "section.md";
    std::filesystem::remove(sectionTarget);
    mqt::core::ExportTask section({src, sectionTarget,
        mqt::core::ExportFormat::Gfm, mqt::core::ByteRange{0, 1024}, 1 << 30});
    const auto sectionBytes = section.run();
    require(sectionBytes == 1024, "section export writes exactly the range");

    std::filesystem::remove_all(root);
}

} // namespace

int main()
{
    try {
        testFrontMatter();
        testGfmExport();
        testTxtExport();
        testSafeHtml();
        testCancelAndFailureSafety();
        std::cout << "export tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
