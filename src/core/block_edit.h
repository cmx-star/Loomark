#pragma once

#include "core/markdown_block.h"
#include "core/document_backend.h"

#include <string>

namespace mqt::core {

/// M34 可编辑块契约：块级修改 → 源码 TextEdit 的映射。
/// 所有写入都带版本校验；往返不发生静默语义变化（重解析后块种类不变）。
enum class BlockEditError {
    None,
    BlockNotFound,
    UnsupportedBlock,        // M36：扩展块不重写，需走源码入口
    RangeLockedByTier,       // M37：大档位仅允许当前章节编辑
    StaleVersion,
    RangeInvalid,
};

struct BlockEditRequest {
    BlockId blockId = kInvalidBlockId;
    std::string newBlockSource; // 替换后的完整块源码（可多行）
    DocumentVersion baseVersion = kInitialDocumentVersion;
    bool confirmed = false;     // 预留：大编辑确认门
    /// M37：大档位下用户当前所在章节的任一偏移（门控基准）
    std::uint64_t currentSectionOffset = 0;
};

struct BlockEditOutcome {
    BlockEditError error = BlockEditError::None;
    ApplyResult apply{};
    std::uint64_t newVersion = kInitialDocumentVersion;
};

/// M37 章节级编辑门控：返回当前偏移是否允许编辑。
/// NORMAL 档全文可编辑；LARGE/EXTREME 仅允许当前标题章节（[章节起点, 下一章节起点)）。
[[nodiscard]] bool isOffsetEditableForTier(FileTier tier, std::uint64_t offset,
    const MarkdownBlockIndex& index, std::uint64_t currentSectionOffset);

/// M34/M35/M36：应用块编辑。
/// 行为：
/// - Heading/Paragraph/ListItem/Quote/CodeFence/Table：允许（替换块源码范围）
/// - Html/HorizontalRule/未知块：UnsupportedBlock（保留原文，走源码入口）
/// - 版本不一致：StaleVersion
/// - 大档位越出当前章节：RangeLockedByTier
[[nodiscard]] BlockEditOutcome applyBlockEdit(const MarkdownBlockIndex& index,
    const std::string& documentSource, FileTier tier,
    const BlockEditRequest& request, IDocumentBackend& backend);

// ---- M38 四层主题 ----
enum class ThemeLayer { App, Editor, Reading, Export };

struct ThemeDefinition {
    std::string name;
    // 简化的颜色集（完整设计在主题批次细化）
    std::string background;
    std::string foreground;
    std::string accent;
};

class ThemeRegistry {
public:
    ThemeRegistry();
    /// 注册/覆盖某层主题；损坏的定义（空名或空前景色）被拒绝返回 false。
    bool registerTheme(ThemeLayer layer, const ThemeDefinition& theme);
    /// 选择主题；未注册或损坏时回退到该层默认主题。
    bool selectTheme(ThemeLayer layer, const std::string& name);
    [[nodiscard]] const ThemeDefinition& activeTheme(ThemeLayer layer) const;
    [[nodiscard]] std::vector<std::string> themeNames(ThemeLayer layer) const;

private:
    std::vector<ThemeDefinition>& layer(ThemeLayer layer);
    [[nodiscard]] const std::vector<ThemeDefinition>& layer(ThemeLayer layer) const;
    std::vector<ThemeDefinition> themes_[4];
    std::string active_[4];
};

} // namespace mqt::core
