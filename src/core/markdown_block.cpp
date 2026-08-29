#include "core/markdown_block.h"

namespace mqt::core {
namespace {

bool isFenceLine(std::string_view line, std::string_view& marker)
{
    // 围栏行：≥3 个 ``` 或 ~~~（允许前导空格）
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    if (i + 3 > line.size()) {
        return false;
    }
    const char fenceChar = line[i];
    if (fenceChar != '`' && fenceChar != '~') {
        return false;
    }
    std::size_t run = 0;
    while (i + run < line.size() && line[i + run] == fenceChar) {
        ++run;
    }
    if (run < 3) {
        return false;
    }
    marker = line.substr(i, run);
    return true;
}

bool isHorizontalRule(std::string_view line)
{
    std::size_t dash = 0, star = 0, underscore = 0, other = 0;
    for (char c : line) {
        if (c == '-') ++dash;
        else if (c == '*') ++star;
        else if (c == '_') ++underscore;
        else if (c == ' ' || c == '\t') continue;
        else ++other;
    }
    if (other != 0) return false;
    return (dash >= 3 && star == 0 && underscore == 0) ||
           (star >= 3 && dash == 0 && underscore == 0) ||
           (underscore >= 3 && dash == 0 && star == 0);
}

bool isTableLine(std::string_view line)
{
    return !line.empty() && line.front() == '|' &&
        line.find('|', 1) != std::string_view::npos;
}

bool isListLine(std::string_view line)
{
    std::size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    if (i + 1 < line.size() && (line[i] == '-' || line[i] == '*' || line[i] == '+') &&
        line[i + 1] == ' ') {
        return true;
    }
    // 有序列表 "1. " / "1) "
    std::size_t digits = 0;
    while (i + digits < line.size() && line[i + digits] >= '0' && line[i + digits] <= '9') {
        ++digits;
    }
    return digits > 0 && i + digits + 1 < line.size() &&
        (line[i + digits] == '.' || line[i + digits] == ')') && line[i + digits + 1] == ' ';
}

bool isQuoteLine(std::string_view line)
{
    std::size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    return i < line.size() && line[i] == '>';
}

bool isHtmlLine(std::string_view line)
{
    std::size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    return i < line.size() && line[i] == '<' && i + 1 < line.size() &&
        line[i + 1] != '!' && line[i + 1] != '/' &&
        ((line[i + 1] >= 'a' && line[i + 1] <= 'z') ||
         (line[i + 1] >= 'A' && line[i + 1] <= 'Z'));
}

int headingLevel(std::string_view line)
{
    std::size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    if (i >= line.size() || line[i] != '#') {
        return 0;
    }
    std::size_t level = 0;
    while (i + level < line.size() && line[i + level] == '#') {
        ++level;
    }
    if (level > 6 || i + level >= line.size() || line[i + level] != ' ') {
        return 0;
    }
    return static_cast<int>(level);
}

} // namespace

std::uint64_t blockHash(std::string_view text)
{
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (unsigned char c : text) {
        h ^= c;
        h *= 0x100000001B3ULL;
    }
    return h;
}

void MarkdownBlockIndex::build(std::string_view content, std::uint64_t documentVersion)
{
    blocks_.clear();
    version_ = documentVersion;

    BlockId nextId = 1;
    std::vector<std::string> headingStack; // (文本, 层级) 用并行栈
    std::vector<int> headingLevels;

    // 块切分：按逻辑行扫描；块 = 连续同类行
    std::size_t lineStart = 0;
    std::size_t pos = 0;
    bool insideFence = false;
    std::string fenceMarker;
    std::size_t fenceStart = 0;

    auto lineEnd = [&](std::size_t from) {
        const auto nl = content.find('\n', from);
        return nl == std::string_view::npos ? content.size() : nl;
    };
    auto lineText = [&](std::size_t from, std::size_t to) {
        std::string_view line = content.substr(from, to - from);
        while (!line.empty() && (line.back() == '\r')) {
            line.remove_suffix(1);
        }
        return line;
    };

    std::size_t blockStartLine = 0;
    BlockKind currentKind = BlockKind::Paragraph;

    auto emitBlock = [&](std::size_t startOffset, std::size_t endOffset, BlockKind kind,
                         int level, const std::vector<std::string>& path,
                         std::string_view text) {
        MarkdownBlock block;
        block.blockId = nextId++;
        block.documentVersion = documentVersion;
        block.kind = kind;
        block.sourceRange = {startOffset, endOffset};
        block.level = level;
        block.headingPath = path;
        block.hash = blockHash(text);
        block.payload = std::string(text.substr(0, std::min<std::size_t>(text.size(), 120)));
        blocks_.push_back(std::move(block));
    };

    while (pos <= content.size()) {
        if (pos == content.size() && lineStart >= content.size()) {
            break;
        }
        const std::size_t end = lineEnd(lineStart);
        const std::string_view line = lineText(lineStart, end);

        if (insideFence) {
            std::string_view marker;
            const bool isClose = isFenceLine(line, marker) && marker.rfind(fenceMarker, 0) == 0;
            // 本行是否为文档最后一行（允许尾随换行）
            const bool lastLine = end + 1 >= content.size() || end >= content.size();
            if (isClose) {
                insideFence = false;
                emitBlock(fenceStart, std::min(end + 1, content.size()), BlockKind::CodeFence,
                    0, {}, content.substr(fenceStart, std::min(end + 1, content.size()) - fenceStart));
            } else if (lastLine) {
                // 未闭合围栏：作为代码块收敛（M23 安全检查点语义）
                emitBlock(fenceStart, content.size(), BlockKind::CodeFence, 0, {},
                    content.substr(fenceStart));
                lineStart = content.size() + 1;
                pos = lineStart;
                break;
            }
            lineStart = end + 1;
            pos = lineStart;
            continue;
        }

        int level = headingLevel(line);
        std::string_view fenceM;
        if (level > 0) {
            // 标题路径 = 祖先标题 + 自身（弹出同级及更深后入栈）
            while (!headingLevels.empty() && headingLevels.back() >= level) {
                headingLevels.pop_back();
                headingStack.pop_back();
            }
            auto ownPath = headingStack;
            ownPath.push_back(std::string(line.substr(0, std::min<std::size_t>(line.size(), 120))));
            headingStack.push_back(ownPath.back());
            headingLevels.push_back(level);
            emitBlock(lineStart, std::min(end + 1, content.size()), BlockKind::Heading, level,
                ownPath, line);
            lineStart = end + 1;
            pos = lineStart;
            continue;
        }
        if (isFenceLine(line, fenceM)) {
            insideFence = true;
            fenceMarker = fenceM;
            fenceStart = lineStart;
            lineStart = end + 1;
            pos = lineStart;
            continue;
        }
        if (line.empty()) {
            // 空行结束当前段落类块
            if (blockStartLine != 0 || (lineStart == 0 && false)) {
                // 段落已在遇到空行时结束——此处仅重置
            }
            lineStart = end + 1;
            pos = lineStart;
            continue;
        }

        BlockKind kind;
        int blockLevel = 0;
        if (isHorizontalRule(line)) {
            emitBlock(lineStart, std::min(end + 1, content.size()), BlockKind::HorizontalRule,
                0, {}, line);
            lineStart = end + 1;
            pos = lineStart;
            continue;
        }
        if (isTableLine(line)) {
            kind = BlockKind::Table;
        } else if (isQuoteLine(line)) {
            kind = BlockKind::Quote;
        } else if (isListLine(line)) {
            kind = BlockKind::ListItem;
            blockLevel = 0; // 缩进层级简化为 0（扩展信息，后续批次再细化）
        } else if (isHtmlLine(line)) {
            kind = BlockKind::Html;
        } else {
            kind = BlockKind::Paragraph;
        }

        // 连续同类行合并为一块（段落/引用/列表/表格）
        std::size_t blockEnd = end;
        std::size_t nextStart = end + 1;
        std::size_t scan = nextStart;
        while (scan <= content.size()) {
            if (scan == content.size()) {
                nextStart = content.size() + 1;
                blockEnd = content.size();
                break;
            }
            const std::size_t e2 = lineEnd(scan);
            const std::string_view l2 = lineText(scan, e2);
            if (l2.empty() || headingLevel(l2) > 0 || isHorizontalRule(l2) ||
                isFenceLine(l2, fenceM)) {
                blockEnd = scan;
                nextStart = scan;
                break;
            }
            bool same = false;
            switch (kind) {
            case BlockKind::Table: same = isTableLine(l2); break;
            case BlockKind::Quote: same = isQuoteLine(l2); break;
            case BlockKind::ListItem: same = isListLine(l2); break;
            case BlockKind::Html: same = isHtmlLine(l2); break;
            default: same = !l2.empty() && headingLevel(l2) == 0; break;
            }
            if (!same) {
                blockEnd = scan;
                nextStart = scan;
                break;
            }
            scan = e2 + 1;
        }
        emitBlock(lineStart, blockEnd, kind, blockLevel, headingStack,
            content.substr(lineStart, blockEnd - lineStart));
        lineStart = nextStart > content.size() ? content.size() + 1 : nextStart;
        pos = lineStart;
        if (lineStart > content.size()) {
            break;
        }
    }
    (void)blockStartLine;
    (void)currentKind;
    (void)fenceStart;
}

const MarkdownBlock* MarkdownBlockIndex::blockAt(std::uint64_t offset) const
{
    const MarkdownBlock* best = nullptr;
    for (const auto& block : blocks_) {
        if (block.sourceRange.start <= offset && offset < block.sourceRange.end) {
            return &block;
        }
        if (block.sourceRange.start <= offset) {
            best = &block;
        }
    }
    return best;
}

std::vector<std::pair<ByteRange, std::vector<std::string>>>
MarkdownBlockIndex::headingOutline() const
{
    std::vector<std::pair<ByteRange, std::vector<std::string>>> outline;
    for (const auto& block : blocks_) {
        if (block.kind == BlockKind::Heading) {
            outline.push_back({block.sourceRange, block.headingPath});
        }
    }
    return outline;
}

void LineScanState::recordCheckpoint(std::uint64_t offset)
{
    FenceCheckpoint cp;
    cp.offset = offset;
    cp.line = line_;
    cp.insideFence = insideFence_;
    cp.fenceMarker = fenceMarker_;
    checkpoints_.push_back(std::move(cp));
}

void LineScanState::feed(std::string_view chunk, std::uint64_t chunkStartOffset)
{
    // 逐完整行处理（跨块行通过 partial_ 携带）；行号按换行符计数
    std::size_t pos = 0;
    while (pos < chunk.size()) {
        const auto nl = chunk.find('\n', pos);
        if (nl == std::string_view::npos) {
            partial_.append(chunk.substr(pos));
            offset_ = chunkStartOffset + chunk.size();
            return;
        }
        std::string line = std::move(partial_);
        partial_.clear();
        line.append(chunk.substr(pos, nl - pos));
        processFenceLine(line);
        ++newlines_;
        if (newlines_ % kCheckpointStride == 0) {
            recordCheckpoint(chunkStartOffset + nl + 1);
        }
        pos = nl + 1;
    }
    offset_ = chunkStartOffset + chunk.size();
    started_ = true;
}

void LineScanState::processFenceLine(std::string_view line)
{
    std::string_view marker;
    if (insideFence_) {
        if (isFenceLine(line, marker) && marker.rfind(fenceMarker_, 0) == 0) {
            insideFence_ = false;
            fenceMarker_.clear();
        }
    } else if (isFenceLine(line, marker)) {
        insideFence_ = true;
        fenceMarker_ = std::string(marker);
    }
}

void LineScanState::finish()
{
    line_ = offset_ == 0 ? 0 : newlines_ + 1;
    recordCheckpoint(offset_);
    (void)started_;
}

FenceCheckpoint LineScanState::nearestCheckpointBefore(
    const std::vector<FenceCheckpoint>& checkpoints, std::uint64_t offset)
{
    FenceCheckpoint best;
    for (const auto& cp : checkpoints) {
        if (cp.offset <= offset && cp.offset >= best.offset) {
            best = cp;
        }
    }
    return best;
}

std::uint64_t IndexScheduler::requestNewVersion()
{
    ++latest_;
    return latest_;
}

bool IndexScheduler::submitResult(std::uint64_t resultVersion)
{
    return resultVersion == latest_ && !cancelled_;
}

} // namespace mqt::core
