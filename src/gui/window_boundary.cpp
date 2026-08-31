#include "gui/window_boundary.h"

#include <algorithm>

namespace mqt::gui {
namespace {

bool isUtf8Continuation(unsigned char ch)
{
    return (ch & 0xC0) == 0x80;
}

std::size_t utf8SequenceLength(unsigned char ch)
{
    if ((ch & 0x80) == 0) {
        return 1;
    }
    if ((ch & 0xE0) == 0xC0) {
        return 2;
    }
    if ((ch & 0xF0) == 0xE0) {
        return 3;
    }
    if ((ch & 0xF8) == 0xF0) {
        return 4;
    }
    return 0;
}

} // namespace

std::size_t utf8SafePrefixLength(std::string_view data)
{
    if (data.empty()) {
        return 0;
    }

    std::size_t continuationBytes = 0;
    const std::size_t limit = std::min<std::size_t>(3, data.size());
    while (continuationBytes < limit &&
        isUtf8Continuation(static_cast<unsigned char>(data[data.size() - 1 - continuationBytes]))) {
        ++continuationBytes;
    }
    if (continuationBytes == data.size()) {
        return 0;
    }

    const std::size_t leadIndex = data.size() - continuationBytes - 1;
    const std::size_t sequenceLength =
        utf8SequenceLength(static_cast<unsigned char>(data[leadIndex]));
    if (sequenceLength == 1) {
        return data.size();
    }
    if (sequenceLength == 0 || sequenceLength != continuationBytes + 1) {
        return leadIndex;
    }
    return data.size();
}

std::size_t safeWindowContentLength(std::string_view data, std::size_t targetBytes)
{
    const std::size_t available = std::min(targetBytes, data.size());
    std::size_t contentLength = utf8SafePrefixLength(data.substr(0, available));

    if (contentLength == available && contentLength > 0 && contentLength < data.size() &&
        data[contentLength - 1] == '\r' && data[contentLength] == '\n') {
        --contentLength;
    }
    return contentLength;
}

} // namespace mqt::gui
