#include "gui/window_boundary.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testUtf8SafePrefixLength()
{
    require(mqt::gui::utf8SafePrefixLength("ascii") == 5, "ASCII must remain intact");
    require(mqt::gui::utf8SafePrefixLength(std::string("\xC3\xA9", 2)) == 2,
        "complete two-byte UTF-8 must remain intact");
    require(mqt::gui::utf8SafePrefixLength(std::string("\xE4\xB8\xAD", 3)) == 3,
        "complete three-byte UTF-8 must remain intact");
    require(mqt::gui::utf8SafePrefixLength(std::string("\xF0\x9F\x98\x80", 4)) == 4,
        "complete four-byte UTF-8 must remain intact");
    require(mqt::gui::utf8SafePrefixLength(std::string("A\xE4\xB8", 3)) == 1,
        "partial UTF-8 sequence must be removed from the prefix");
    require(mqt::gui::utf8SafePrefixLength(std::string("\x80", 1)) == 0,
        "orphan continuation byte must not be exposed");
}

void testCrLfLookahead()
{
    require(mqt::gui::safeWindowContentLength("abc\r\nrest", 4) == 3,
        "CRLF split at the target boundary must move to the next window");
    require(mqt::gui::safeWindowContentLength("abc\rX", 4) == 4,
        "standalone CR must remain in the current window");
    require(mqt::gui::safeWindowContentLength("abc\r", 4) == 4,
        "CR at end of file must remain in the current window");
}

} // namespace

int main()
{
    try {
        testUtf8SafePrefixLength();
        testCrLfLookahead();
        std::cout << "window boundary tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
