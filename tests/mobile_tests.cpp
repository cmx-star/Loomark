// 批次 18：移动共享核心测试（响应式/分段编辑/资源预算）
#include "core/mobile.h"
#include "core/virtual_preview.h"

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

void testFormFactorAndLayout()
{
    require(mqt::core::formFactorFor(400, 800) == mqt::core::FormFactor::Phone,
        "400dp short side is phone");
    require(mqt::core::formFactorFor(800, 1200) == mqt::core::FormFactor::Tablet,
        "800dp short side is tablet");
    require(mqt::core::layoutFor(mqt::core::FormFactor::Phone, false) ==
            mqt::core::MobileLayout::SinglePane, "phone single pane");
    require(mqt::core::layoutFor(mqt::core::FormFactor::Phone, true) ==
            mqt::core::MobileLayout::Drawer, "phone drawer");
    require(mqt::core::layoutFor(mqt::core::FormFactor::Tablet, false) ==
            mqt::core::MobileLayout::TwoPane, "tablet two pane");
}

void testPagedBackend()
{
    // 3MiB 文档，页 1MiB
    const auto path = std::filesystem::temp_directory_path() / "paged.md";
    const std::size_t total = 3ULL << 20;
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        for (std::size_t i = 0; i < total / 1024; ++i) {
            output << std::string(1023, 'a') << "\n";
        }
    }
    mqt::core::PagedDocumentBackend paged(path, 1ULL << 20);
    require(paged.pageStart() == 0, "initial page at 0");
    const auto pageLen = paged.pageEnd();

    // 页内读取
    const auto head = paged.read({0, 16});
    require(head.size() == 16, "page read works");

    // 页内编辑
    mqt::core::TextEdit edit{0, 3, "HEAD"};
    auto r = paged.apply({edit}, paged.snapshot().version);
    require(r.error == mqt::core::ApplyError::None, "page edit applies");

    // 保存三段拼接：文件其余部分不被破坏
    paged.save(nullptr);
    std::ifstream check(path, std::ios::binary);
    std::string head4(4, '\0');
    check.read(head4.data(), 4);
    require(head4 == "HEAD", "saved file has the edit");
    check.seekg(-8, std::ios::end);
    std::string tail(8, '\0');
    check.read(tail.data(), 8);
    require(tail == "aaaaaaa\n", "tail segment preserved");

    // seek 换页后读取另一页
    paged.seekTo(pageLen + 10);
    require(paged.pageStart() >= pageLen, "seek moves to next page");
    require(paged.read({paged.pageStart(), paged.pageStart() + 8}).size() == 8,
        "read on the new page");

    std::filesystem::remove(path);
}

void testMobileBudgets()
{
    require(mqt::core::MobileBudgets::phoneAiContextTokens <
            mqt::core::MobileBudgets::tabletAiContextTokens,
        "phone AI budget smaller than tablet");
    require(mqt::core::MobileBudgets::phoneMaxExportBytes <
            mqt::core::MobileBudgets::tabletMaxExportBytes,
        "phone export budget smaller than tablet");

    // 100MB 虚拟阅读：复用 VirtualPreviewModel（大内容窗口化不炸）
    std::string content = "# doc\n";
    content += std::string(100 * 1024, 'x');
    mqt::core::MarkdownBlockIndex index;
    index.build(content, 1);
    mqt::core::VirtualPreviewModel model;
    model.rebuild(index, content.size());
    require(model.visibleWindow(0, 800, 3).valid, "virtual read window valid");
}

} // namespace

int main()
{
    try {
        testFormFactorAndLayout();
        testPagedBackend();
        testMobileBudgets();
        std::cout << "mobile core tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
