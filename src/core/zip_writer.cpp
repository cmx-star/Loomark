#include "core/zip_writer.h"

namespace mqt::core {

namespace {
void putU16(std::string& out, std::uint16_t v)
{
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void putU32(std::string& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}
} // namespace

std::uint32_t crc32(std::string_view bytes)
{
    static std::uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        init = true;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : bytes) {
        crc = table[(crc ^ c) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

void ZipWriter::addFile(const std::string& name, std::string_view bytes)
{
    Entry entry;
    entry.name = name;
    entry.crc = crc32(bytes);
    entry.size = static_cast<std::uint32_t>(bytes.size());
    entry.offset = static_cast<std::uint32_t>(data_.size());

    putU32(data_, 0x04034b50);
    putU16(data_, 20);       // version
    putU16(data_, 0);        // flags
    putU16(data_, 0);        // method = store
    putU16(data_, 0); putU16(data_, 0); // time/date
    putU32(data_, entry.crc);
    putU32(data_, entry.size);
    putU32(data_, entry.size);
    putU16(data_, static_cast<std::uint16_t>(name.size()));
    putU16(data_, 0);
    data_.append(name);
    data_.append(bytes);
    entries_.push_back(std::move(entry));
}

std::string ZipWriter::finish()
{
    std::string out = std::move(data_);
    const std::uint32_t centralStart = static_cast<std::uint32_t>(out.size());
    for (const auto& e : entries_) {
        putU32(out, 0x02014b50);
        putU16(out, 20); putU16(out, 20);
        putU16(out, 0); putU16(out, 0);
        putU16(out, 0); putU16(out, 0);
        putU32(out, e.crc);
        putU32(out, e.size);
        putU32(out, e.size);
        putU16(out, static_cast<std::uint16_t>(e.name.size()));
        putU16(out, 0); putU16(out, 0); putU16(out, 0);
        putU32(out, 0);
        putU32(out, e.offset);
        out.append(e.name);
    }
    const std::uint32_t centralSize =
        static_cast<std::uint32_t>(out.size()) - centralStart;
    putU32(out, 0x06054b50);
    putU16(out, 0); putU16(out, 0);
    putU16(out, static_cast<std::uint16_t>(entries_.size()));
    putU16(out, static_cast<std::uint16_t>(entries_.size()));
    putU32(out, centralSize);
    putU32(out, centralStart);
    putU16(out, 0);
    return out;
}

} // namespace mqt::core
