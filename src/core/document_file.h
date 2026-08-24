#pragma once

#include "core/byte_range.h"
#include "core/file_tier.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace mqt::core {

enum class NewlineStyle {
    None,
    LF,
    CRLF,
    CR,
    Mixed
};

struct FileInfo {
    std::filesystem::path path;
    std::uint64_t sizeBytes = 0;
    FileTier tier = FileTier::Normal;
    bool hasUtf8Bom = false;
    NewlineStyle newlineStyle = NewlineStyle::None;
};

struct TextPosition {
    std::uint64_t line = 1;
    std::uint64_t column = 1;
};

struct SearchHit {
    ByteRange sourceRange;
    TextPosition position;
};

struct SearchOptions {
    std::size_t chunkSize = 64 * 1024;
    std::size_t maxResults = 1000;
};

struct SearchResult {
    std::vector<SearchHit> hits;
    std::uint64_t bytesScanned = 0;
    bool truncated = false;
};

struct LocateResult {
    TextPosition start;
    TextPosition end;
};

[[nodiscard]] std::string_view toString(NewlineStyle style);
[[nodiscard]] FileInfo inspectFile(const std::filesystem::path& path);
[[nodiscard]] std::string readRange(const std::filesystem::path& path, ByteRange range);
[[nodiscard]] SearchResult searchLiteral(const std::filesystem::path& path, std::string_view needle, const SearchOptions& options = {});
[[nodiscard]] LocateResult locateByteRange(const std::filesystem::path& path, ByteRange range);
void writeFileAtomically(const std::filesystem::path& path, std::string_view content);

/// Returns free disk space in bytes for the volume that contains `path`.
/// The path itself does not need to exist, but its parent directory must be
/// resolvable. Throws std::runtime_error when the volume cannot be queried so
/// callers never mistake an unknown state for a safe one.
[[nodiscard]] std::uint64_t availableDiskBytes(const std::filesystem::path& path);

} // namespace mqt::core
