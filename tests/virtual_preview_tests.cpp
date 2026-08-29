// 批次 7：虚拟预览模型 / 大块二次虚拟化 / 目录模型 / 定位锚点 测试
#include "core/virtual_preview.h"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string buildSample()
{
    std::string content = "# Top\n";
    for (int i = 0; i < 40; ++i) {
        content += "paragraph line " + std::to_string(i) + "\n";
    }
    content += "## Section A\n";
    for (int i = 0; i < 40; ++i) {
        content += "section a line " + std::to_string(i) + "\n";
    }
    content += "## Section B\n";
    // 一个超大代码块（300KB）触发二次虚拟化
    content += "```code\n" + std::string(300 * 1024, 'x') + "\n```\n";
    content += "tail line\n";
    return content;
}

void testVirtualWindow()
{
    const auto content = buildSample();
    mqt::core::MarkdownBlockIndex index;
    index.build(content, 1);
    mqt::core::VirtualPreviewModel model;
    model.rebuild(index, content.size());
    require(model.itemCount() >= 5, "items built");
    require(model.totalHeight() > 1000, "height estimated");

    // 顶部窗口
    const auto top = model.visibleWindow(0, 800, 3);
    require(top.valid && top.first == 0, "top window starts at 0");
    require(top.last >= top.first, "window range valid");

    // 底部窗口
    const auto bottom = model.visibleWindow(model.totalHeight() - 400, 800, 3);
    require(bottom.valid && bottom.last == model.itemCount() - 1,
        "bottom window reaches the last item");

    // 中部窗口 + 预加载带
    const auto mid = model.visibleWindow(model.totalHeight() / 2, 800, 3);
    require(mid.last > mid.first, "mid window spans items");
    require(mid.last - mid.first <= 40, "window bounded");
}

void testOversizedSplit()
{
    mqt::core::ByteRange big{1000, 1000 + 300 * 1024};
    const auto windows = mqt::core::OversizedBlockSplitter::split(big, 64 * 1024);
    require(windows.size() == 5, "300KiB / 64KiB → 5 sub-windows");
    require(windows.front().sourceRange.start == 1000, "first sub-window start");
    require(windows.back().sourceRange.end == 1000 + 300 * 1024,
        "last sub-window end");
    require(windows.back().index == windows.back().count - 1, "sub-window indices");
}

void testOutlineAndLocator()
{
    const auto content = buildSample();
    mqt::core::MarkdownBlockIndex index;
    index.build(content, 1);

    mqt::core::OutlineModel outline;
    outline.rebuild(index);
    require(outline.entryCount() == 3, "three headings");
    require(outline.entries()[0].text == "Top", "outline text stripped of #");
    require(outline.entries()[1].level == 2, "section A is level 2");
    require(outline.entries()[1].sourceRange.start > outline.entries()[0].sourceRange.end,
        "outline ordered by document position");

    mqt::core::SourceLocator locator;
    locator.rebuild(index, content.size());
    // 源码偏移 → 锚点 → 回到源码偏移
    const std::uint64_t probe = content.size() / 2;
    const auto anchor = locator.anchorForOffset(probe);
    require(anchor.blockId != mqt::core::kInvalidBlockId, "anchor inside a real block");
    const auto back = locator.offsetForAnchor(anchor.blockId, anchor.ratioInBlock);
    require(back == probe, "anchor round-trip preserves offset");

    // 越界偏移钳制到文档末尾
    const auto tailAnchor = locator.anchorForOffset(content.size() + 1000);
    require(locator.offsetForAnchor(tailAnchor.blockId, tailAnchor.ratioInBlock) <=
            content.size() + 1,
        "tail anchor clamped");
}

} // namespace

int main()
{
    try {
        testVirtualWindow();
        testOversizedSplit();
        testOutlineAndLocator();
        std::cout << "virtual preview tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
