#include "core/document_file.h"

#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

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
