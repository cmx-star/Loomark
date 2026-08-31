#include "core/document_backend.h"

#include "core/markdown_block.h"

#include <cstring>

#include <algorithm>
#include <deque>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace mqt::core {
namespace {

std::vector<std::size_t> buildPrefixTable(std::string_view needle)
{
    std::vector<std::size_t> prefix(needle.size(), 0);
    std::size_t matched = 0;
    for (std::size_t i = 1; i < needle.size(); ++i) {
        while (matched > 0 && needle[i] != needle[matched]) {
            matched = prefix[matched - 1];
        }
        if (needle[i] == needle[matched]) {
            ++matched;
        }
        prefix[i] = matched;
    }
    return prefix;
}

struct Cursor {
    std::uint64_t offset = 0;
    std::uint64_t line = 1;
    std::uint64_t column = 1;
};

void advanceCursor(Cursor& cursor, unsigned char ch, bool zeroWidth)
{
    ++cursor.offset;
    if (zeroWidth) {
        return;
    }
    if (ch == '\n') {
        ++cursor.line;
        cursor.column = 1;
    } else {
        ++cursor.column;
    }
}

struct CursorSample {
    std::uint64_t offset = 0;
    TextPosition position {};
};

} // namespace

FileDocumentBackend::FileDocumentBackend(const std::filesystem::path& path)
    : path_(path)
{
    loadFromFile();
}

DocumentSnapshot FileDocumentBackend::snapshot() const
{
    return {version_, info_};
}

DocumentInfo FileDocumentBackend::info() const
{
    return info_;
}

std::string FileDocumentBackend::read(ByteRange range) const
{
    const auto sz = range.size();
    if (sz > buffer_.size()) {
        throw std::out_of_range("byte range exceeds content size");
    }
    if (range.start > buffer_.size() - sz) {
        throw std::out_of_range("byte range exceeds content size");
    }
    return buffer_.substr(static_cast<std::size_t>(range.start), static_cast<std::size_t>(sz));
}

LocateResult FileDocumentBackend::locateLines(ByteRange range) const
{
    if (range.end > buffer_.size()) {
        throw std::out_of_range("byte range exceeds content size");
    }
    const std::uint64_t bodyStart = range.start;
    const std::uint64_t bodyEnd = range.end;
    if (bodyStart > buffer_.size() || bodyEnd > buffer_.size()) {
        throw std::out_of_range("byte range exceeds content size");
    }

    Cursor cursor;
    LocateResult result;
    bool startCaptured = false;
    bool endCaptured = false;

    for (std::size_t i = 0; i < buffer_.size(); ++i) {
        if (!startCaptured && cursor.offset == bodyStart) {
            result.start = {cursor.line, cursor.column};
            startCaptured = true;
        }
        if (!endCaptured && cursor.offset == bodyEnd) {
            result.end = {cursor.line, cursor.column};
            endCaptured = true;
            if (startCaptured) {
                break;
            }
        }
        advanceCursor(cursor,
            static_cast<unsigned char>(buffer_[i]),
            false);
    }

    if (!startCaptured || !endCaptured) {
        throw std::runtime_error("failed to locate line positions");
    }
    return result;
}

SearchOutcome FileDocumentBackend::search(const SearchQuery& query,
    const std::atomic_bool* cancelFlag) const
{
    if (query.needle.empty()) {
        throw std::invalid_argument("search needle cannot be empty");
    }
    if (query.options.chunkSize == 0 || query.options.maxResults == 0) {
        throw std::invalid_argument("search options must be non-zero");
    }

    SearchResult result;
    const auto prefix = buildPrefixTable(query.needle);
    std::vector<char> buf(query.options.chunkSize);
    std::size_t matched = 0;
    Cursor cursor;
    std::deque<CursorSample> samples;
    samples.push_back({0, {1, 1}});

    std::uint64_t i = 0;
    while (i < buffer_.size() && !result.truncated) {
        if (cancelFlag != nullptr && cancelFlag->load(std::memory_order_relaxed)) {
            return {result, true};
        }
        const auto toRead = std::min<std::uint64_t>(query.options.chunkSize, buffer_.size() - i);
        std::memcpy(buf.data(), buffer_.data() + i, toRead);
        i += toRead;

        for (std::uint64_t j = 0; j < toRead; ++j) {
            const unsigned char ch = static_cast<unsigned char>(buf[j]);
            samples.push_back({cursor.offset, {cursor.line, cursor.column}});
            bool stopAfterCurrentByte = false;

            while (matched > 0 && ch != static_cast<unsigned char>(query.needle[matched])) {
                matched = prefix[matched - 1];
            }
            if (ch == static_cast<unsigned char>(query.needle[matched])) {
                ++matched;
            }

            const auto startLimit = cursor.offset >= query.needle.size()
                ? cursor.offset - query.needle.size() + 1 : 0;
            while (!samples.empty() && samples.front().offset < startLimit) {
                samples.pop_front();
            }

            if (matched == query.needle.size()) {
                if (result.hits.size() >= query.options.maxResults) {
                    result.truncated = true;
                    stopAfterCurrentByte = true;
                } else {
                    if (samples.empty() || samples.front().offset != startLimit) {
                        throw std::runtime_error("search cursor alignment lost");
                    }
                    result.hits.push_back(SearchHit{
                        .sourceRange = {startLimit + bomOffset_, cursor.offset + 1 + bomOffset_},
                        .position = samples.front().position,
                    });
                    matched = prefix[matched - 1];
                }
            }

            advanceCursor(cursor, ch, false);
            if (stopAfterCurrentByte) {
                break;
            }
        }
    }

    result.bytesScanned = cursor.offset;
    return {result, false};
}

ApplyResult FileDocumentBackend::apply(std::vector<TextEdit> edits,
    DocumentVersion baseVersion,
    const std::atomic_bool* cancelFlag)
{
    (void)cancelFlag;
    if (baseVersion != version_) {
        return {ApplyError::StaleVersion, version_};
    }

    if (edits.empty()) {
        return {ApplyError::None, version_};
    }

    std::uint64_t prevEnd = 0;
    for (const auto& edit : edits) {
        if (edit.start > edit.end) {
            return {ApplyError::RangeInvalid, version_};
        }
        if (edit.end > buffer_.size()) {
            return {ApplyError::RangeInvalid, version_};
        }
        if (edit.start < prevEnd) {
            return {ApplyError::OverlappingEdits, version_};
        }
        prevEnd = edit.end;
    }

    std::string newBuffer;
    newBuffer.reserve(buffer_.size());
    std::uint64_t cursor = 0;
    for (const auto& edit : edits) {
        if (edit.start > cursor) {
            newBuffer.append(buffer_.data() + cursor, static_cast<std::size_t>(edit.start - cursor));
        }
        newBuffer.append(edit.newText);
        cursor = edit.end;
    }
    if (cursor < buffer_.size()) {
        newBuffer.append(buffer_.data() + cursor, static_cast<std::size_t>(buffer_.size() - cursor));
    }

    previousBuffer_ = buffer_;
    canUndoFlag_ = true;

    buffer_ = std::move(newBuffer);
    info_.sizeBytes = buffer_.size();
    fingerprint_ = blockHash(buffer_);
    ++version_;
    return {ApplyError::None, version_};
}

void FileDocumentBackend::save(const std::atomic_bool*)
{
    saveToFile();
}

void FileDocumentBackend::saveAs(const std::filesystem::path& path)
{
    path_ = path;
    saveToFile();
}

DocumentSnapshot FileDocumentBackend::reload()
{
    loadFromFile();
    ++version_;
    return snapshot();
}

void FileDocumentBackend::loadFromFile()
{
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path_, ec);
    if (ec) {
        throw std::runtime_error("failed to read file size: " + path_.string());
    }
    if (classifyDesktopFile(fileSize) != FileTier::Normal) {
        throw std::runtime_error("file exceeds Normal tier limit: " + path_.string());
    }

    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path_.string());
    }

    std::string raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

    bomOffset_ = 0;
    if (raw.size() >= 3 &&
        static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF) {
        bomOffset_ = 3;
        raw.erase(raw.begin(), raw.begin() + 3);
    }

    buffer_ = std::move(raw);
    info_.tier = FileTier::Normal;
    info_.sizeBytes = buffer_.size();
    info_.hasUtf8Bom = bomOffset_ > 0;
    info_.newlineStyle = NewlineStyle::None;
    info_.newlineStyleKnown = false;
    fingerprint_ = blockHash(buffer_);
    canUndoFlag_ = false;
}

bool FileDocumentBackend::undo()
{
    if (!canUndoFlag_) {
        return false;
    }
    redoBuffer_ = buffer_;
    buffer_ = previousBuffer_;
    info_.sizeBytes = buffer_.size();
    fingerprint_ = blockHash(buffer_);
    canUndoFlag_ = false;
    ++version_;
    return true;
}

bool FileDocumentBackend::redo()
{
    if (redoBuffer_.empty()) {
        return false;
    }
    buffer_ = redoBuffer_;
    redoBuffer_.clear();
    info_.sizeBytes = buffer_.size();
    fingerprint_ = blockHash(buffer_);
    canUndoFlag_ = true;
    ++version_;
    return true;
}

bool FileDocumentBackend::canUndo() const
{
    return canUndoFlag_;
}

void FileDocumentBackend::saveToFile()
{
    const auto dir = path_.parent_path().empty()
        ? std::filesystem::current_path()
        : path_.parent_path();
    const auto freeBytes = availableDiskBytes(dir);
    const auto needed = buffer_.size() + (info_.hasUtf8Bom ? 3ULL : 0ULL) + 2ULL * kMiB;
    if (freeBytes < needed) {
        throw std::runtime_error("insufficient disk space to save document");
    }

    AtomicFileWriter writer(path_);
    if (info_.hasUtf8Bom) {
        writer.write("\xEF\xBB\xBF");
    }
    writer.write(buffer_);
    writer.commit();
    info_.sizeBytes = buffer_.size() + (info_.hasUtf8Bom ? 3ULL : 0ULL);
}

} // namespace mqt::core
