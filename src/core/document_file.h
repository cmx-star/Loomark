#pragma once

#include "core/byte_range.h"
#include "core/file_tier.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
    /// True once newlineStyle was determined by a full inspectFile() scan.
    bool newlineStyleKnown = false;
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

/// Fast, O(1)-bytes metadata probe: size, tier, and BOM presence. Never scans
/// the whole file, so it is safe to call on the UI thread even for very large
/// documents. The returned newline style is undetermined
/// (newlineStyleKnown == false); run inspectFile() for the full analysis.
[[nodiscard]] FileInfo statFile(const std::filesystem::path& path);

/// Full inspection including newline-style classification, which requires
/// reading every byte. Run off the UI thread for large files. When
/// `cancelFlag` is set, returns early with newlineStyleKnown == false.
[[nodiscard]] FileInfo inspectFile(const std::filesystem::path& path,
    const std::atomic_bool* cancelFlag = nullptr);
[[nodiscard]] std::string readRange(const std::filesystem::path& path, ByteRange range);
[[nodiscard]] SearchResult searchLiteral(const std::filesystem::path& path, std::string_view needle, const SearchOptions& options = {});
[[nodiscard]] LocateResult locateByteRange(const std::filesystem::path& path, ByteRange range);
void writeFileAtomically(const std::filesystem::path& path, std::string_view content);

/// Streams chunks into a temp file next to `path` and atomically replaces
/// `path` on commit(), so writers never need the full payload in memory.
/// The destructor aborts (removes the temp file) unless commit() completed,
/// therefore partially written data never becomes visible.
class AtomicFileWriter {
public:
    explicit AtomicFileWriter(const std::filesystem::path& path);
    ~AtomicFileWriter();
    AtomicFileWriter(const AtomicFileWriter&) = delete;
    AtomicFileWriter& operator=(const AtomicFileWriter&) = delete;

    void write(std::string_view chunk);
    void commit();

private:
    std::filesystem::path targetPath_;
    std::filesystem::path tempPath_;
    std::ofstream output_;
    std::uint64_t bytesWritten_ = 0;
    bool committed_ = false;
};

/// Returns free disk space in bytes for the volume that contains `path`.
/// The path itself does not need to exist, but its parent directory must be
/// resolvable. Throws std::runtime_error when the volume cannot be queried so
/// callers never mistake an unknown state for a safe one.
[[nodiscard]] std::uint64_t availableDiskBytes(const std::filesystem::path& path);

} // namespace mqt::core
