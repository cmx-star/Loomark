#pragma once

#include <cstdint>
#include <stdexcept>

namespace mqt::core {

struct ByteRange {
    std::uint64_t start = 0;
    std::uint64_t end = 0;

    [[nodiscard]] std::uint64_t size() const
    {
        if (end < start) {
            throw std::invalid_argument("byte range end is before start");
        }
        return end - start;
    }
};

} // namespace mqt::core
