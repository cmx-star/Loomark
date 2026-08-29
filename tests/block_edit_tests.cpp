// 批次 9：可编辑块契约 / 章节门控 / 主题 测试
#include "core/block_edit.h"
#include "core/document_backend.h"

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

void testBlockEditRoundTrip()
{
    const std::string content = "# Title\n\npara one\npara two\n\n- item\n";
    {
        std::ofstream output("/tmp/block-edit.md", std::ios::binary | std::ios::trunc);
        output << content;
    }
    mqt::core::FileDocumentBackend backend(std::filesystem::path("/tmp") / "block-edit.md");

    mqt::core::MarkdownBlockIndex index;
    index.build(content, backend.snapshot().version);

    // 找到段落块并改写
    const mqt::core::MarkdownBlock* para = nullptr;
    for (const auto& b : index.blocks()) {
        if (b.kind == mqt::core::BlockKind::Paragraph) {
            para = &b;
            break;
        }
    }
    require(para != nullptr, "paragraph block found");

    mqt::core::BlockEditRequest req;
    req.blockId = para->blockId;
    req.newBlockSource = "para one rewritten\npara two still here\n";
    req.baseVersion = backend.snapshot().version;
    const auto outcome = mqt::core::applyBlockEdit(index, content,
        mqt::core::FileTier::Normal, req, backend);
    require(outcome.error == mqt::core::BlockEditError::None, "block edit applies");

    // 往返：重解析后块种类保持（无静默语义变化）
    const auto newContent = backend.read({mqt::core::ByteRange{0,
        backend.info().sizeBytes}});
    mqt::core::MarkdownBlockIndex reParsed;
    reParsed.build(newContent, backend.snapshot().version);
    bool sameKindPresent = false;
    for (const auto& b : reParsed.blocks()) {
        if (b.kind == mqt::core::BlockKind::Paragraph &&
            b.payload.find("rewritten") != std::string::npos) {
            sameKindPresent = true;
        }
    }
    require(sameKindPresent, "rewritten paragraph survives re-parse as a paragraph");
    require(outcome.newVersion == mqt::core::kInitialDocumentVersion + 1,
        "block edit bumps version");
}

void testUnsupportedAndStale()
{
    const std::string content = "---\n\npara\n";
    {
        std::ofstream output("/tmp/block-edit2.md", std::ios::binary | std::ios::trunc);
        output << content;
    }
    mqt::core::FileDocumentBackend backend(std::filesystem::path("/tmp") / "block-edit2.md");
    mqt::core::MarkdownBlockIndex index;
    index.build(content, 1);

    // HR 块不支持重写
    const mqt::core::MarkdownBlock* hr = nullptr;
    const mqt::core::MarkdownBlock* para = nullptr;
    for (const auto& b : index.blocks()) {
        if (b.kind == mqt::core::BlockKind::HorizontalRule) hr = &b;
        if (b.kind == mqt::core::BlockKind::Paragraph) para = &b;
    }
    require(hr != nullptr && para != nullptr, "hr and para found");

    mqt::core::BlockEditRequest bad;
    bad.blockId = hr->blockId;
    bad.newBlockSource = "===";
    bad.baseVersion = backend.snapshot().version;
    auto outcome = mqt::core::applyBlockEdit(index, content,
        mqt::core::FileTier::Normal, bad, backend);
    require(outcome.error == mqt::core::BlockEditError::UnsupportedBlock,
        "HR block must fall back to source entry");

    // 版本过期
    mqt::core::BlockEditRequest stale;
    stale.blockId = para->blockId;
    stale.newBlockSource = "changed";
    stale.baseVersion = 99;
    outcome = mqt::core::applyBlockEdit(index, content,
        mqt::core::FileTier::Normal, stale, backend);
    require(outcome.error == mqt::core::BlockEditError::StaleVersion,
        "stale version rejected");

    // 未知块
    mqt::core::BlockEditRequest missing;
    missing.blockId = 99999;
    missing.baseVersion = backend.snapshot().version;
    outcome = mqt::core::applyBlockEdit(index, content,
        mqt::core::FileTier::Normal, missing, backend);
    require(outcome.error == mqt::core::BlockEditError::BlockNotFound,
        "unknown block rejected");
}

void testTierGating()
{
    const std::string content =
        "# Section 1\npara a\n\n# Section 2\npara b\n\n# Section 3\npara c\n";
    mqt::core::MarkdownBlockIndex index;
    index.build(content, 1);
    const std::uint64_t offsetA = content.find("para a");
    const std::uint64_t offsetB = content.find("para b");
    const std::uint64_t offsetC = content.find("para c");
    const std::uint64_t offsetEnd = content.size() - 1;

    const std::uint64_t cursorInSection2 = content.find("# Section 2");
    const std::uint64_t cursorInSection1 = content.find("# Section 1");
    require(mqt::core::isOffsetEditableForTier(mqt::core::FileTier::Normal,
        offsetB, index, cursorInSection2), "Normal tier edits anywhere");
    require(mqt::core::isOffsetEditableForTier(mqt::core::FileTier::Large,
        offsetB, index, cursorInSection2), "Large tier edits inside the current section");
    require(!mqt::core::isOffsetEditableForTier(mqt::core::FileTier::Large,
        offsetA, index, cursorInSection2),
        "Large tier cannot edit outside the current section");
    // 光标回到 Section 1 后，offsetA 可编辑
    require(mqt::core::isOffsetEditableForTier(mqt::core::FileTier::Large,
        offsetA, index, cursorInSection1),
        "editable range follows the cursor's section");
    // Section 3 的 para c 在光标位于 Section 2 时不可编辑
    require(!mqt::core::isOffsetEditableForTier(mqt::core::FileTier::Extreme,
        offsetC, index, cursorInSection2),
        "Extreme tier cannot edit another section");
}

void testThemes()
{
    mqt::core::ThemeRegistry registry;
    require(registry.activeTheme(mqt::core::ThemeLayer::Editor).name ==
            "editor-default", "default theme active");

    mqt::core::ThemeDefinition dark;
    dark.name = "editor-dark";
    dark.background = "#000000";
    dark.foreground = "#ffffff";
    dark.accent = "#ffcc00";
    require(registry.registerTheme(mqt::core::ThemeLayer::Editor, dark),
        "valid theme registered");
    require(registry.selectTheme(mqt::core::ThemeLayer::Editor, "editor-dark"),
        "theme selected");
    require(registry.activeTheme(mqt::core::ThemeLayer::Editor).background == "#000000",
        "theme colors applied");

    // 损坏主题被拒绝
    mqt::core::ThemeDefinition broken;
    broken.name = "";
    require(!registry.registerTheme(mqt::core::ThemeLayer::App, broken),
        "corrupt theme registration rejected");

    // 未注册主题选择 → 回退默认
    require(!registry.selectTheme(mqt::core::ThemeLayer::Editor, "nonexistent"),
        "unknown theme selection falls back");
    require(registry.activeTheme(mqt::core::ThemeLayer::Editor).name ==
            "editor-default", "fallback to default theme");

    // 四层独立
    mqt::core::ThemeDefinition reading;
    reading.name = "sepia";
    reading.foreground = "#5b4636";
    require(registry.registerTheme(mqt::core::ThemeLayer::Reading, reading),
        "reading theme registered");
    require(registry.selectTheme(mqt::core::ThemeLayer::Reading, "sepia"),
        "reading theme selected");
    require(registry.activeTheme(mqt::core::ThemeLayer::Editor).name == "editor-default",
        "layers are independent");
}

} // namespace

int main()
{
    try {
        testBlockEditRoundTrip();
        testUnsupportedAndStale();
        testTierGating();
        testThemes();
        std::cout << "block edit tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
