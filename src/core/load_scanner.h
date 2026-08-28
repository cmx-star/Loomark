#pragma once

#include "core/document_file.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace mqt::core {

/// Streaming FNV-1a 64-bit content fingerprint. Computed while a document
/// loads so M12 can compare against a saved copy for external modifications.
class FingerprintSink {
public:
    void update(std::string_view chunk)
    {
        for (unsigned char c : chunk) {
            hash_ ^= c;
            hash_ *= 0x100000001B3ULL;
        }
        updated_ = true;
    }

    [[nodiscard]] std::uint64_t value() const { return hash_; }
    /// True once any bytes were fed. Empty content is still deterministic.
    [[nodiscard]] bool updated() const { return updated_; }

private:
    std::uint64_t hash_ = 0xCBF29CE484222325ULL;
    bool updated_ = false;
};

/// Byte-level line-end statistics with cross-chunk state. A trailing CR at
/// end-of-file counts as a CR line end once finish() is called.
struct NewlineCounts {
    std::uint64_t lf = 0;
    std::uint64_t crlf = 0;
    std::uint64_t cr = 0;
};

class NewlineCounter {
public:
    void feed(std::string_view chunk)
    {
        for (char c : chunk) {
            if (lastWasCR_) {
                lastWasCR_ = false;
                if (c == '\n') {
                    ++counts_.crlf;
                    continue;
                }
                ++counts_.cr;
            }
            if (c == '\r') {
                lastWasCR_ = true;
            } else if (c == '\n') {
                ++counts_.lf;
            }
        }
    }

    void finish()
    {
        if (lastWasCR_) {
            ++counts_.cr;
            lastWasCR_ = false;
        }
    }

    [[nodiscard]] NewlineCounts counts() const { return counts_; }

private:
    NewlineCounts counts_{};
    bool lastWasCR_ = false;
};

/// Majority vote over the counters; aligns with normalizeLineEndings
/// semantics (LF wins ties over CR, CRLF wins ties over LF).
[[nodiscard]] NewlineStyle classifyNewlines(const NewlineCounts& counts);
[[nodiscard]] bool newlineStyleKnown(const NewlineCounts& counts);

/// Sparse line index recorded while a document streams in. Only every
/// kStride-th line start is stored (plus the first line and an end sentinel),
/// which keeps a 1M-line document at a few KB instead of 8+ MB.
class SparseLineIndex {
public:
    static constexpr std::uint64_t kStride = 256;

    struct Anchor {
        std::uint64_t line = 0;   // 0-based
        std::uint64_t offset = 0; // byte offset of the line start
        bool exact = false;       // true when `line` itself is an anchor
    };

    void clear();
    /// Record the line start. Call in strictly increasing line order.
    void addAnchor(std::uint64_t line, std::uint64_t offset);
    /// Final line count (0-based; a document with n lines reports n).
    void setLineCount(std::uint64_t count) { lineCount_ = count; }
    /// Sentinel: offset one past the last content byte.
    void setEndOffset(std::uint64_t offset) { endOffset_ = offset; }

    [[nodiscard]] std::uint64_t lineCount() const { return lineCount_; }
    [[nodiscard]] std::uint64_t endOffset() const { return endOffset_; }

    /// Nearest stored anchor at or before `line`. Line 0 is always present.
    [[nodiscard]] Anchor anchorAtOrBeforeLine(std::uint64_t line) const;
    /// Nearest stored anchor whose offset <= `offset`.
    [[nodiscard]] Anchor anchorAtOrBeforeOffset(std::uint64_t offset) const;

private:
    // Parallel arrays: lines_ strictly increasing, multiples of kStride plus 0.
    std::vector<std::uint64_t> lines_;
    std::vector<std::uint64_t> offsets_;
    std::uint64_t lineCount_ = 0;
    std::uint64_t endOffset_ = 0;
};

/// Incremental line-start detector fed with arbitrary chunk boundaries.
/// Multi-byte UTF-8 sequences never contain 0x0A/0x0D bytes, so a pure byte
/// scan is boundary-safe. `chunkStart` is the absolute byte offset of the
/// chunk's first byte in the document.
class LineIndexBuilder {
public:
    void feed(std::string_view chunk, std::uint64_t chunkStart);
    /// Call after the last chunk: records the end sentinel and total lines.
    void finish(std::uint64_t endOffset);

    [[nodiscard]] const SparseLineIndex& index() const { return index_; }

private:
    SparseLineIndex index_;
    std::uint64_t absolute_ = 0;  // bytes fed so far
    std::uint64_t newlines_ = 0;  // newline bytes fed so far
    bool first_ = true;
};

} // namespace mqt::core
