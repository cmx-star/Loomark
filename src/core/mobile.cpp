#include "core/mobile.h"

#include <algorithm>
#include <fstream>

namespace mqt::core {

PagedDocumentBackend::PagedDocumentBackend(std::filesystem::path path,
    std::uint64_t pageSize)
    : path_(std::move(path))
    , pageSize_(pageSize == 0 ? (1ULL << 20) : pageSize)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path_, ec);
    const auto info = statFile(path_);
    info_ = DocumentInfo{info.tier, info.sizeBytes - (info.hasUtf8Bom ? 3ULL : 0ULL),
        info.hasUtf8Bom, NewlineStyle::None, false};
    bomOffset_ = info.hasUtf8Bom ? 3ULL : 0ULL;
    (void)size;
    loadPage(0);
}

void PagedDocumentBackend::loadPage(std::uint64_t offset)
{
    // BOM 只在文档首部出现：从内容偏移换算文件偏移
    const std::uint64_t contentOffset = offset;
    const std::uint64_t fileOffset = contentOffset + (contentOffset == 0 ? bomOffset_ : 0);
    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path_.string());
    }
    input.seekg(static_cast<std::streamoff>(fileOffset));
    std::string raw;
    raw.resize(pageSize_);
    input.read(raw.data(), static_cast<std::streamsize>(pageSize_));
    raw.resize(static_cast<std::size_t>(input.gcount()));

    // 页边界 UTF-8 / CRLF 安全：回退到完整字节（这里简化为只在 '\n' 处截断）
    if (raw.size() == pageSize_ && contentOffset + raw.size() < info_.sizeBytes) {
        const auto lastNl = raw.rfind('\n');
        if (lastNl != std::string::npos && lastNl > 0) {
            raw.resize(lastNl + 1);
        }
    }

    pageStart_ = contentOffset;
    pageEnd_ = contentOffset + raw.size();
    originalPageEnd_ = pageEnd_;
    buffer_ = std::move(raw);
}

DocumentSnapshot PagedDocumentBackend::snapshot() const
{
    return {version_, info_};
}

DocumentInfo PagedDocumentBackend::info() const
{
    return info_;
}

std::string PagedDocumentBackend::read(ByteRange range) const
{
    const auto sz = range.size();
    if (range.start < pageStart_ || range.end > pageEnd_) {
        throw std::out_of_range("read outside the loaded page (seek first)");
    }
    return buffer_.substr(static_cast<std::size_t>(range.start - pageStart_),
        static_cast<std::size_t>(sz));
}

LocateResult PagedDocumentBackend::locateLines(ByteRange range) const
{
    if (range.end > pageEnd_ || range.start < pageStart_) {
        throw std::out_of_range("locate outside the loaded page");
    }
    // 简化实现：页内行扫描
    std::uint64_t line = 1, column = 1;
    std::uint64_t startLine = 0, startCol = 0, endLine = 0, endCol = 0;
    bool startFound = false, endFound = false;
    for (std::size_t i = 0; i < buffer_.size(); ++i) {
        const std::uint64_t offset = pageStart_ + i;
        if (!startFound && offset == range.start) {
            startLine = line; startCol = column; startFound = true;
        }
        if (!endFound && offset == range.end) {
            endLine = line; endCol = column; endFound = true;
            break;
        }
        if (buffer_[i] == '\n') {
            ++line; column = 1;
        } else {
            ++column;
        }
    }
    if (!startFound || !endFound) {
        throw std::runtime_error("failed to locate line positions");
    }
    return LocateResult{{startLine, startCol}, {endLine, endCol}};
}

SearchOutcome PagedDocumentBackend::search(const SearchQuery& query,
    const std::atomic_bool* cancelFlag) const
{
    SearchResult result;
    std::size_t pos = 0;
    std::uint64_t line = 1;
    while (pos <= buffer_.size() &&
        result.hits.size() < query.options.maxResults) {
        if (cancelFlag != nullptr && cancelFlag->load(std::memory_order_relaxed)) {
            return {result, true};
        }
        const auto found = buffer_.find(query.needle, pos);
        if (found == std::string::npos) {
            break;
        }
        result.hits.push_back(SearchHit{
            {pageStart_ + found, pageStart_ + found + query.needle.size()},
            {line, 1}});
        pos = found + query.needle.size();
        ++result.bytesScanned;
        line += static_cast<std::uint64_t>(
            std::count(buffer_.begin() + static_cast<std::ptrdiff_t>(found),
                buffer_.begin() + static_cast<std::ptrdiff_t>(pos), '\n'));
    }
    result.bytesScanned = buffer_.size();
    return {result, false};
}

ApplyResult PagedDocumentBackend::apply(std::vector<TextEdit> edits,
    DocumentVersion baseVersion, const std::atomic_bool*)
{
    if (baseVersion != version_) {
        return {ApplyError::StaleVersion, version_};
    }
    if (edits.empty()) {
        return {ApplyError::None, version_};
    }
    // 页内相对偏移校验与排序（升序、不重叠）
    std::uint64_t prevEnd = 0;
    for (auto& edit : edits) {
        // 偏移换算到页内
        if (edit.start > edit.end || edit.end > pageEnd_ || edit.start < pageStart_) {
            return {ApplyError::RangeInvalid, version_};
        }
        if (edit.start < prevEnd) {
            return {ApplyError::OverlappingEdits, version_};
        }
        prevEnd = edit.end;
    }
    // 应用到页缓冲（升序 → 从后往前）
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        const std::size_t start = static_cast<std::size_t>(it->start - pageStart_);
        const std::size_t count = static_cast<std::size_t>(it->end - it->start);
        buffer_.replace(start, count, it->newText);
    }
    pageEnd_ = pageStart_ + buffer_.size();
    info_.sizeBytes = buffer_.size();
    ++version_;
    return {ApplyError::None, version_};
}

void PagedDocumentBackend::save(const std::atomic_bool*)
{
    saveAs(path_);
}

void PagedDocumentBackend::saveAs(const std::filesystem::path& path)
{
    const auto dir = path.parent_path().empty()
        ? std::filesystem::current_path()
        : path.parent_path();
    const auto freeBytes = availableDiskBytes(dir);
    const auto needed = info_.sizeBytes + (info_.hasUtf8Bom ? 3ULL : 0ULL) + 2ULL * kMiB;
    if (freeBytes < needed) {
        throw std::runtime_error("insufficient disk space to save document");
    }
    AtomicFileWriter writer(path);
    if (info_.hasUtf8Bom) {
        writer.write("\xEF\xBB\xBF");
    }
    const auto totalNow = std::filesystem::file_size(path_);
    // 头段（0..装载时页起点）。pageStart 是内容偏移，BOM 只存在于文件头部。
    const std::uint64_t headBytes = pageStart_ + bomOffset_;
    if (headBytes > 0) {
        std::ifstream head(path_, std::ios::binary);
        std::string headBuf;
        headBuf.resize(static_cast<std::size_t>(headBytes));
        head.read(headBuf.data(), static_cast<std::streamsize>(headBytes));
        writer.write(headBuf);
    }
    writer.write(buffer_);
    // 尾段（装载时页尾..文档尾）—— 页内编辑只改变 buffer_ 长度，
    // 尾段偏移必须取装载时的页尾（originalPageEnd_ + BOM）。
    const std::uint64_t tailFrom = originalPageEnd_ + bomOffset_;
    if (tailFrom < totalNow) {
        std::ifstream tail(path_, std::ios::binary);
        tail.seekg(static_cast<std::streamoff>(tailFrom));
        std::string tailBuf;
        tailBuf.resize(static_cast<std::size_t>(totalNow - tailFrom));
        tail.read(tailBuf.data(), static_cast<std::streamsize>(tailBuf.size()));
        writer.write(tailBuf);
    }
    writer.commit();
    // 保存后重读当前页，使缓冲与磁盘一致（后续编辑基于新基线）
    loadPage(pageStart_);
    savedVersion_ = version_;
    info_.sizeBytes = info_.hasUtf8Bom ? buffer_.size() + 3 : buffer_.size();
}

DocumentSnapshot PagedDocumentBackend::reload()
{
    loadPage(pageStart_);
    ++version_;
    savedVersion_ = version_;
    return snapshot();
}

void PagedDocumentBackend::seekTo(std::uint64_t offset)
{
    if (offset >= pageStart_ && offset < pageEnd_) {
        return; // 已在当前页
    }
    loadPage(offset);
    savedVersion_ = version_;
}

} // namespace mqt::core
