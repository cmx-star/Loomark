// 批次 6：块模型/解析适配/安全检查点/索引调度 测试
#include "core/markdown_block.h"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testBlockParsing()
{
    const std::string content =
        "# Title\n"
        "\n"
        "para one\n"
        "para one continues\n"
        "\n"
        "- item 1\n"
        "- item 2\n"
        "\n"
        "> quote line\n"
        "> quote more\n"
        "\n"
        "| a | b |\n"
        "| c | d |\n"
        "\n"
        "---\n"
        "\n"
        "```python\n"
        "code with # not a heading\n"
        "```\n"
        "\n"
        "<div>\n"
        "html\n"
        "</div>\n"
        "\n"
        "## Sub\n"
        "tail paragraph\n";

    mqt::core::MarkdownBlockIndex index;
    index.build(content, 7);
    require(index.version() == 7, "version recorded");
    require(index.blockCount() > 8, "blocks parsed");

    // 标题块
    const auto& blocks = index.blocks();
    require(blocks[0].kind == mqt::core::BlockKind::Heading && blocks[0].level == 1,
        "first block is level-1 heading");
    require(blocks[0].payload == "# Title", "heading payload is the line");

    // 段落合并
    std::size_t paraOne = 1;
    require(blocks[paraOne].kind == mqt::core::BlockKind::Paragraph,
        "second block is a paragraph");
    require(blocks[paraOne].sourceRange.end - blocks[paraOne].sourceRange.start ==
            std::string("para one\npara one continues\n").size(),
        "paragraph merges consecutive lines");

    // 围栏内的 # 不是标题
    bool sawFence = false;
    for (const auto& b : blocks) {
        if (b.kind == mqt::core::BlockKind::CodeFence) {
            sawFence = true;
            require(b.sourceRange.start < b.sourceRange.end, "fence range valid");
        }
    }
    require(sawFence, "code fence block present");
    bool headingInsideFence = false;
    for (const auto& b : blocks) {
        if (b.kind == mqt::core::BlockKind::Heading &&
            b.payload.find("not a heading") != std::string::npos) {
            headingInsideFence = true;
        }
    }
    require(!headingInsideFence, "heading inside fence must not be a heading");

    // blockAt
    const auto* at = index.blockAt(blocks[paraOne].sourceRange.start + 2);
    require(at != nullptr && at->kind == mqt::core::BlockKind::Paragraph,
        "blockAt finds the paragraph");

    // 标题路径
    auto outline = index.headingOutline();
    require(outline.size() == 2, "two headings in outline");
}

void testHeadingPath()
{
    const std::string content =
        "# A\n"
        "text\n"
        "## B\n"
        "text\n"
        "### C\n"
        "# D\n";
    mqt::core::MarkdownBlockIndex index;
    index.build(content, 1);
    const auto& blocks = index.blocks();
    const mqt::core::MarkdownBlock* b = nullptr;
    const mqt::core::MarkdownBlock* c = nullptr;
    const mqt::core::MarkdownBlock* d = nullptr;
    for (const auto& blk : blocks) {
        if (blk.kind == mqt::core::BlockKind::Heading) {
            if (blk.level == 2) b = &blk;
            if (blk.level == 3) c = &blk;
            if (blk.level == 1 && blk.payload == "# D") d = &blk;
        }
    }
    require(b != nullptr && b->headingPath.size() == 2 &&
            b->headingPath[0] == "# A" && b->headingPath[1] == "## B",
        "level-2 heading path is [A, B]");
    require(c != nullptr && c->headingPath.size() == 3,
        "level-3 heading path is [A, B, C]");
    require(d != nullptr && d->headingPath.size() == 1 && d->headingPath[0] == "# D",
        "level-1 heading resets the path");
}

void testUnclosedFence()
{
    const std::string content =
        "# Doc\n"
        "```js\n"
        "unterminated code\n";
    mqt::core::MarkdownBlockIndex index;
    index.build(content, 1);
    bool fenceFound = false;
    for (const auto& b : index.blocks()) {
        if (b.kind == mqt::core::BlockKind::CodeFence) {
            fenceFound = true;
        }
    }
    require(fenceFound, "unclosed fence converges into a code block");
}

void testCheckpoints()
{
    mqt::core::LineScanState scan;
    std::string content;
    for (int i = 0; i < 600; ++i) {
        content += "plain line\n";
        if (i == 300) {
            content += "```\nfence\n```\n";
        }
    }
    scan.feed(content, 0);
    scan.finish();
    require(scan.checkpoints().size() >= 2, "checkpoints recorded at stride");

    // 最近检查点：位于 300 行围栏之后的偏移应记录 insideFence=false
    const auto mid = mqt::core::LineScanState::nearestCheckpointBefore(
        scan.checkpoints(), content.size() / 2);
    require(mid.offset > 0, "checkpoint before mid offset found");

    // 600 行纯文本 + 围栏闭合：最后一个检查点不在围栏内
    const auto last = scan.checkpoints().back();
    require(!last.insideFence, "final checkpoint outside fence");

    // 跨块喂入：分两个 chunk 结果一致
    mqt::core::LineScanState s2;
    s2.feed(content.substr(0, 1000), 0);
    s2.feed(content.substr(1000), 1000);
    s2.finish();
    require(s2.lineCount() == scan.lineCount(), "chunked feed line count matches");
    require(s2.checkpoints().size() == scan.checkpoints().size(),
        "chunked feed checkpoints match");
}

void testCheckpointAcrossFenceBoundary()
{
    // 围栏行 "```" 恰被切块切开时，围栏状态仍需正确收敛
    std::string content;
    for (int i = 0; i < 100; ++i) {
        content += "filler\n";
    }
    content += "```\ncode\n```\ntail\n";
    mqt::core::LineScanState a, b;
    a.feed(content, 0);
    a.finish();
    // 切分点选在 "``" 与 "`" 之间
    const std::size_t split = content.rfind("```") + 2;
    b.feed(content.substr(0, split), 0);
    b.feed(content.substr(split), split);
    b.finish();
    require(b.lineCount() == a.lineCount(), "fence-boundary chunked line count matches");
    require(b.insideFence() == a.insideFence(), "fence state converges across chunk split");
}

void testScheduler()
{
    mqt::core::IndexScheduler sched;
    require(sched.submitResult(1), "initial version accepted");
    const auto v2 = sched.requestNewVersion();
    require(v2 == 2, "new version requested");
    require(!sched.submitResult(1), "stale result rejected");
    require(sched.submitResult(2), "current result accepted");
    sched.cancelAll();
    require(sched.cancelled(), "cancelled");
    require(!sched.submitResult(2), "cancelled scheduler rejects results");
}

} // namespace

int main()
{
    try {
        testBlockParsing();
        testHeadingPath();
        testUnclosedFence();
        testCheckpoints();
        testCheckpointAcrossFenceBoundary();
        testScheduler();
        std::cout << "block model tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
