#pragma once

#include <cstdint>
#include <string_view>

namespace mqt::core {

enum class FileTier {
    Normal,
    Large,
    Extreme,
    Reject
};

inline constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;
inline constexpr std::uint64_t kNormalFileLimit = 64ULL * kMiB;
inline constexpr std::uint64_t kLargeFileLimit = 256ULL * kMiB;
inline constexpr std::uint64_t kExtremeFileLimit = 512ULL * kMiB;

[[nodiscard]] FileTier classifyDesktopFile(std::uint64_t sizeBytes);
[[nodiscard]] std::string_view toString(FileTier tier);

} // namespace mqt::core
