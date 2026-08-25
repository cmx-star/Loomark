#pragma once

#include <cstddef>
#include <string_view>

namespace mqt::gui {

/// Returns the largest prefix that does not end inside a UTF-8 sequence.
[[nodiscard]] std::size_t utf8SafePrefixLength(std::string_view data);

/// Computes the editable portion of a window buffer. `data` may contain one
/// lookahead byte beyond `targetBytes` so a CRLF pair is kept in one window.
[[nodiscard]] std::size_t safeWindowContentLength(std::string_view data, std::size_t targetBytes);

} // namespace mqt::gui
