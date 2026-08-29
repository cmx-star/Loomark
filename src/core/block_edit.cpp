#include "core/block_edit.h"

#include <algorithm>

namespace mqt::core {

namespace {
bool editableKind(BlockKind kind)
{
    switch (kind) {
    case BlockKind::Heading:
    case BlockKind::Paragraph:
    case BlockKind::ListItem:
    case BlockKind::Quote:
    case BlockKind::Table:
    case BlockKind::CodeFence:
        return true;
    case BlockKind::Html:
    case BlockKind::HorizontalRule:
        return false;
    }
    return false;
}
} // namespace

bool isOffsetEditableForTier(FileTier tier, std::uint64_t offset,
    const MarkdownBlockIndex& index, std::uint64_t currentSectionOffset)
{
    if (tier == FileTier::Normal) {
        return true; // 全文可编辑
    }
    // 大档位：仅「当前章节」（包含 currentSectionOffset 的标题章节）可编辑。
    const MarkdownBlock* sectionStart = nullptr;
    const MarkdownBlock* sectionEnd = nullptr;
    for (const auto& block : index.blocks()) {
        if (block.kind != BlockKind::Heading) {
            continue;
        }
        if (block.sourceRange.start <= currentSectionOffset) {
            sectionStart = &block;
            sectionEnd = nullptr;
        } else if (sectionStart != nullptr && block.level <= sectionStart->level) {
            sectionEnd = &block;
            break;
        }
    }
    if (sectionStart == nullptr) {
        return false;
    }
    const std::uint64_t end = sectionEnd != nullptr
        ? sectionEnd->sourceRange.start
        : std::numeric_limits<std::uint64_t>::max();
    return offset >= sectionStart->sourceRange.start && offset < end;
}

BlockEditOutcome applyBlockEdit(const MarkdownBlockIndex& index,
    const std::string& /*documentSource*/, FileTier tier,
    const BlockEditRequest& request, IDocumentBackend& backend)
{
    BlockEditOutcome outcome;
    const MarkdownBlock* target = nullptr;
    for (const auto& block : index.blocks()) {
        if (block.blockId == request.blockId) {
            target = &block;
            break;
        }
    }
    if (target == nullptr) {
        outcome.error = BlockEditError::BlockNotFound;
        return outcome;
    }
    if (!editableKind(target->kind)) {
        outcome.error = BlockEditError::UnsupportedBlock;
        return outcome;
    }
    if (!isOffsetEditableForTier(tier, target->sourceRange.start, index,
        request.currentSectionOffset)) {
        outcome.error = BlockEditError::RangeLockedByTier;
        return outcome;
    }

    std::vector<TextEdit> edits{TextEdit{
        target->sourceRange.start,
        target->sourceRange.end,
        request.newBlockSource,
    }};
    const auto result = backend.apply(std::move(edits), request.baseVersion, nullptr);
    outcome.apply = result;
    outcome.newVersion = result.newVersion;
    switch (result.error) {
    case ApplyError::None: outcome.error = BlockEditError::None; break;
    case ApplyError::StaleVersion: outcome.error = BlockEditError::StaleVersion; break;
    default: outcome.error = BlockEditError::RangeInvalid; break;
    }
    return outcome;
}

// ---- M38 主题 ----

namespace {
const ThemeDefinition& defaultTheme(ThemeLayer layer)
{
    static const ThemeDefinition defaults[4] = {
        {"app-default", "#1f2024", "#e6e6e6", "#4a9eff"},
        {"editor-default", "#141518", "#dcdcdc", "#61afef"},
        {"reading-default", "#fafafa", "#222222", "#0366d6"},
        {"export-default", "#ffffff", "#000000", "#000000"},
    };
    return defaults[static_cast<int>(layer)];
}
} // namespace

ThemeRegistry::ThemeRegistry()
{
    for (int i = 0; i < 4; ++i) {
        active_[i] = defaultTheme(static_cast<ThemeLayer>(i)).name;
        themes_[i].push_back(defaultTheme(static_cast<ThemeLayer>(i)));
    }
}

std::vector<ThemeDefinition>& ThemeRegistry::layer(ThemeLayer layer)
{
    return themes_[static_cast<int>(layer)];
}

const std::vector<ThemeDefinition>& ThemeRegistry::layer(ThemeLayer layer) const
{
    return themes_[static_cast<int>(layer)];
}

bool ThemeRegistry::registerTheme(ThemeLayer layer, const ThemeDefinition& theme)
{
    if (theme.name.empty() || theme.foreground.empty()) {
        return false; // 损坏/不完整定义被拒绝
    }
    auto& list = this->layer(layer);
    const auto it = std::find_if(list.begin(), list.end(),
        [&](const ThemeDefinition& t) { return t.name == theme.name; });
    if (it != list.end()) {
        *it = theme;
    } else {
        list.push_back(theme);
    }
    return true;
}

bool ThemeRegistry::selectTheme(ThemeLayer layer, const std::string& name)
{
    const auto& list = this->layer(layer);
    const auto it = std::find_if(list.begin(), list.end(),
        [&](const ThemeDefinition& t) { return t.name == name && !t.foreground.empty(); });
    if (it == list.end()) {
        active_[static_cast<int>(layer)] = defaultTheme(layer).name; // 回退
        return false;
    }
    active_[static_cast<int>(layer)] = name;
    return true;
}

const ThemeDefinition& ThemeRegistry::activeTheme(ThemeLayer layer) const
{
    const auto& list = this->layer(layer);
    const auto it = std::find_if(list.begin(), list.end(),
        [&](const ThemeDefinition& t) { return t.name == active_[static_cast<int>(layer)]; });
    return it != list.end() ? *it : defaultTheme(layer);
}

std::vector<std::string> ThemeRegistry::themeNames(ThemeLayer layer) const
{
    std::vector<std::string> names;
    for (const auto& t : this->layer(layer)) {
        names.push_back(t.name);
    }
    return names;
}

} // namespace mqt::core
