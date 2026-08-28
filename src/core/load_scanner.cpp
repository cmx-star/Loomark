#include "core/load_scanner.h"

namespace mqt::core {

NewlineStyle classifyNewlines(const NewlineCounts& counts)
{
    if (counts.lf + counts.crlf + counts.cr == 0) {
        return NewlineStyle::LF;
    }
    if (counts.crlf >= counts.lf && counts.crlf >= counts.cr) {
        return NewlineStyle::CRLF;
    }
    if (counts.lf >= counts.cr) {
        return NewlineStyle::LF;
    }
    return NewlineStyle::CR;
}

bool newlineStyleKnown(const NewlineCounts& counts)
{
    return counts.lf + counts.crlf + counts.cr > 0;
}

void SparseLineIndex::clear()
{
    lines_.clear();
    offsets_.clear();
    lineCount_ = 0;
    endOffset_ = 0;
}

void SparseLineIndex::addAnchor(std::uint64_t line, std::uint64_t offset)
{
    if (line == 0 || line % kStride == 0) {
        if (!lines_.empty() && lines_.back() == line) {
            return;
        }
        lines_.push_back(line);
        offsets_.push_back(offset);
    }
}

SparseLineIndex::Anchor SparseLineIndex::anchorAtOrBeforeLine(std::uint64_t line) const
{
    if (lines_.empty()) {
        // Empty index (no content fed): line 0 at offset 0.
        return {0, 0, true};
    }
    // lines_ is sorted; find the last anchor <= line. Line 0 is always
    // anchored, so a result always exists.
    std::size_t lo = 0;
    std::size_t hi = lines_.size();
    while (lo + 1 < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (lines_[mid] <= line) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return {lines_[lo], offsets_[lo], lines_[lo] == line};
}

SparseLineIndex::Anchor SparseLineIndex::anchorAtOrBeforeOffset(std::uint64_t offset) const
{
    if (offsets_.empty()) {
        return {0, 0, offset == 0};
    }
    std::size_t lo = 0;
    std::size_t hi = offsets_.size();
    while (lo + 1 < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (offsets_[mid] <= offset) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return {lines_[lo], offsets_[lo], offsets_[lo] == offset};
}

void LineIndexBuilder::feed(std::string_view chunk, std::uint64_t chunkStart)
{
    if (first_) {
        index_.addAnchor(0, chunkStart);
        first_ = false;
    }
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        if (chunk[i] == '\n') {
            // The newline completes the current line; the next line starts
            // at the following byte.
            index_.addAnchor(newlines_ + 1, absolute_ + i + 1);
            ++newlines_;
        }
    }
    absolute_ += chunk.size();
}

void LineIndexBuilder::finish(std::uint64_t endOffset)
{
    index_.setEndOffset(endOffset);
    // 0-based line numbering: a document with n newlines has n+1 lines when
    // any content exists (the trailing partial line counts), else 0 lines.
    index_.setLineCount(absolute_ == 0 ? 0 : newlines_ + 1);
}

} // namespace mqt::core
