#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mqt::core {

/// M50：最小 ZIP 写入器（store 不压缩）——DOCX/OOXML 容器用。
/// 产物符合 PKZIP 结构（local headers + central directory + CRC32）。
class ZipWriter {
public:
    void addFile(const std::string& name, std::string_view bytes);
    [[nodiscard]] std::string finish(); // 返回完整 ZIP 字节流

private:
    struct Entry {
        std::string name;
        std::uint32_t crc = 0;
        std::uint32_t size = 0;
        std::uint32_t offset = 0;
    };
    std::vector<Entry> entries_;
    std::string data_;
};

[[nodiscard]] std::uint32_t crc32(std::string_view bytes);

} // namespace mqt::core
