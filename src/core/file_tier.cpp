#include "core/file_tier.h"

namespace mqt::core {

FileTier classifyDesktopFile(std::uint64_t sizeBytes)
{
    if (sizeBytes <= kNormalFileLimit) {
        return FileTier::Normal;
    }
    if (sizeBytes <= kLargeFileLimit) {
        return FileTier::Large;
    }
    if (sizeBytes <= kExtremeFileLimit) {
        return FileTier::Extreme;
    }
    return FileTier::Reject;
}

std::string_view toString(FileTier tier)
{
    switch (tier) {
    case FileTier::Normal:
        return "normal";
    case FileTier::Large:
        return "large";
    case FileTier::Extreme:
        return "extreme";
    case FileTier::Reject:
        return "reject";
    }
    return "unknown";
}

} // namespace mqt::core
