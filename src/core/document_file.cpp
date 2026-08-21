#include "core/document_file.h"

#include <array>
#include <chrono>
#include <deque>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace mqt::core {
namespace {

struct NewlineCounts {
    std::uint64_t lf = 0;
    std::uint64_t crlf = 0;
    std::uint64_t cr = 0;
};

NewlineStyle classifyNewlines(const NewlineCounts& counts)
{
    const int kinds = (counts.lf > 0 ? 1 : 0) + (counts.crlf > 0 ? 1 : 0) + (counts.cr > 0 ? 1 : 0);
    if (kinds == 0) {
        return NewlineStyle::None;
    }
    if (kinds > 1) {
        return NewlineStyle::Mixed;
    }
    if (counts.crlf > 0) {
        return NewlineStyle::CRLF;
    }
    if (counts.cr > 0) {
        return NewlineStyle::CR;
    }
    return NewlineStyle::LF;
}

std::filesystem::path makeTempPath(const std::filesystem::path& path)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream name;
    name << "." << path.filename().string() << ".tmp." << stamp;
    return path.parent_path() / name.str();
}

struct Cursor {
    std::uint64_t offset = 0;
    std::uint64_t line = 1;
    std::uint64_t column = 1;
};

struct CursorSample {
    std::uint64_t offset = 0;
    TextPosition position {};
};

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

bool hasUtf8Bom(std::ifstream& input)
{
    std::array<char, 3> prefix {};
    input.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
    if (input.gcount() == static_cast<std::streamsize>(prefix.size()) &&
        static_cast<unsigned char>(prefix[0]) == 0xEF &&
        static_cast<unsigned char>(prefix[1]) == 0xBB &&
        static_cast<unsigned char>(prefix[2]) == 0xBF) {
        input.clear();
        input.seekg(0, std::ios::beg);
        return true;
    }

    input.clear();
    input.seekg(0, std::ios::beg);
    return false;
}

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

} // namespace

std::string_view toString(NewlineStyle style)
{
    switch (style) {
    case NewlineStyle::None:
        return "none";
    case NewlineStyle::LF:
        return "lf";
    case NewlineStyle::CRLF:
        return "crlf";
    case NewlineStyle::CR:
        return "cr";
    case NewlineStyle::Mixed:
        return "mixed";
    }
    return "unknown";
}

FileInfo inspectFile(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        throw std::runtime_error("failed to read file size: " + path.string());
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    FileInfo info;
    info.path = path;
    info.sizeBytes = size;
    info.tier = classifyDesktopFile(size);

    std::array<char, 64 * 1024> buffer {};
    bool firstChunk = true;
    bool previousWasCr = false;
    NewlineCounts counts;

    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = input.gcount();
        if (read <= 0) {
            break;
        }

        if (firstChunk) {
            firstChunk = false;
            info.hasUtf8Bom = read >= 3 &&
                static_cast<unsigned char>(buffer[0]) == 0xEF &&
                static_cast<unsigned char>(buffer[1]) == 0xBB &&
                static_cast<unsigned char>(buffer[2]) == 0xBF;
        }

        for (std::streamsize i = 0; i < read; ++i) {
            const char ch = buffer[static_cast<std::size_t>(i)];
            if (previousWasCr) {
                if (ch == '\n') {
                    ++counts.crlf;
                    previousWasCr = false;
                    continue;
                }
                ++counts.cr;
                previousWasCr = false;
            }
            if (ch == '\r') {
                previousWasCr = true;
            } else if (ch == '\n') {
                ++counts.lf;
            }
        }
    }

    if (previousWasCr) {
        ++counts.cr;
    }
    info.newlineStyle = classifyNewlines(counts);
    return info;
}

std::string readRange(const std::filesystem::path& path, ByteRange range)
{
    const auto requested = range.size();
    const auto fileSize = std::filesystem::file_size(path);
    if (range.end > fileSize) {
        throw std::out_of_range("byte range exceeds file size");
    }
    if (requested > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::length_error("byte range is too large for memory buffer");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path.string());
    }
    input.seekg(static_cast<std::streamoff>(range.start), std::ios::beg);
    if (!input) {
        throw std::runtime_error("failed to seek file: " + path.string());
    }

    std::string data(static_cast<std::size_t>(requested), '\0');
    if (!data.empty()) {
        input.read(data.data(), static_cast<std::streamsize>(data.size()));
        if (input.gcount() != static_cast<std::streamsize>(data.size())) {
            throw std::runtime_error("failed to read requested byte range");
        }
    }
    return data;
}

SearchResult searchLiteral(const std::filesystem::path& path, std::string_view needle, const SearchOptions& options)
{
    if (needle.empty()) {
        throw std::invalid_argument("search needle cannot be empty");
    }
    if (options.chunkSize == 0 || options.maxResults == 0) {
        throw std::invalid_argument("search options must be non-zero");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    SearchResult result;
    const auto prefix = buildPrefixTable(needle);
    std::vector<char> buffer(options.chunkSize);
    std::size_t matched = 0;
    Cursor cursor;
    std::deque<CursorSample> samples;
    const bool zeroWidthBom = hasUtf8Bom(input);
    samples.push_back({0, {1, 1}});

    while (input && !result.truncated) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = input.gcount();
        if (read <= 0) {
            break;
        }

        for (std::streamsize i = 0; i < read; ++i) {
            const unsigned char ch = static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
            samples.push_back({cursor.offset, {cursor.line, cursor.column}});
            bool stopAfterCurrentByte = false;

            while (matched > 0 && ch != static_cast<unsigned char>(needle[matched])) {
                matched = prefix[matched - 1];
            }
            if (ch == static_cast<unsigned char>(needle[matched])) {
                ++matched;
            }

            const auto startLimit = cursor.offset >= needle.size() ? cursor.offset - needle.size() + 1 : 0;
            while (!samples.empty() && samples.front().offset < startLimit) {
                samples.pop_front();
            }

            if (matched == needle.size()) {
                if (result.hits.size() >= options.maxResults) {
                    result.truncated = true;
                    stopAfterCurrentByte = true;
                } else {
                    if (samples.empty() || samples.front().offset != startLimit) {
                        throw std::runtime_error("search cursor alignment lost");
                    }
                    result.hits.push_back(SearchHit{
                        .sourceRange = {startLimit, cursor.offset + 1},
                        .position = samples.front().position,
                    });
                    matched = prefix[matched - 1];
                }
            }

            advanceCursor(cursor, ch, zeroWidthBom && cursor.offset < 3);
            if (stopAfterCurrentByte) {
                break;
            }
        }
    }

    result.bytesScanned = cursor.offset;
    return result;
}

LocateResult locateByteRange(const std::filesystem::path& path, ByteRange range)
{
    const auto fileSize = std::filesystem::file_size(path);
    if (range.end > fileSize) {
        throw std::out_of_range("byte range exceeds file size");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    std::vector<char> buffer(64 * 1024);
    Cursor cursor;
    LocateResult result;
    bool startCaptured = false;
    bool endCaptured = false;
    const bool zeroWidthBom = hasUtf8Bom(input);

    while (input && (!startCaptured || !endCaptured)) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = input.gcount();
        if (read <= 0) {
            break;
        }

        for (std::streamsize i = 0; i < read; ++i) {
            if (!startCaptured && cursor.offset == range.start) {
                result.start = {cursor.line, cursor.column};
                startCaptured = true;
            }
            if (!endCaptured && cursor.offset == range.end) {
                result.end = {cursor.line, cursor.column};
                endCaptured = true;
                if (startCaptured) {
                    break;
                }
            }
            advanceCursor(cursor, static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]), zeroWidthBom && cursor.offset < 3);
        }
    }

    if (!startCaptured && cursor.offset == range.start) {
        result.start = {cursor.line, cursor.column};
        startCaptured = true;
    }
    if (!endCaptured && cursor.offset == range.end) {
        result.end = {cursor.line, cursor.column};
        endCaptured = true;
    }

    if (!startCaptured || !endCaptured) {
        throw std::runtime_error("failed to locate byte range positions");
    }
    return result;
}

void writeFileAtomically(const std::filesystem::path& path, std::string_view content)
{
    const auto directory = path.parent_path().empty() ? std::filesystem::current_path() : path.parent_path();
    std::filesystem::create_directories(directory);
    const auto tempPath = makeTempPath(path);

    {
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("failed to create temp file: " + tempPath.string());
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output) {
            std::filesystem::remove(tempPath);
            throw std::runtime_error("failed to write temp file: " + tempPath.string());
        }
    }

    std::error_code ec;
    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        std::filesystem::remove(tempPath);
        throw std::runtime_error("failed to replace file atomically: " + path.string());
    }
}

} // namespace mqt::core
