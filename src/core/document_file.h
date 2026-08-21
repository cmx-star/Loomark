#pragma once

#include "core/byte_range.h"
#include "core/file_tier.h"

#include <cstdint>
#include <filesystem>
#include <string>

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

[[nodiscard]] std::string_view toString(NewlineStyle style);
[[nodiscard]] FileInfo inspectFile(const std::filesystem::path& path);
[[nodiscard]] std::string readRange(const std::filesystem::path& path, ByteRange range);
void writeFileAtomically(const std::filesystem::path& path, std::string_view content);

} // namespace mqt::core
