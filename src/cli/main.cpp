#include "core/document_file.h"
#include "core/markdown_index.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void printUsage()
{
    std::cerr
        << "Usage:\n"
        << "  markdown_qt_p0 inspect <file>\n"
        << "  markdown_qt_p0 index <file> [max-blocks]\n"
        << "  markdown_qt_p0 search <file> <needle> [max-results]\n"
        << "  markdown_qt_p0 locate <file> <start> <end>\n"
        << "  markdown_qt_p0 chunk <file> <start> <end>\n";
}

std::uint64_t parseU64(const char* value)
{
    std::size_t parsed = 0;
    const auto result = std::stoull(value, &parsed, 10);
    if (parsed != std::string(value).size()) {
        throw std::invalid_argument("expected unsigned integer");
    }
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc < 3) {
            printUsage();
            return EXIT_FAILURE;
        }

        const std::string command = argv[1];
        const std::filesystem::path path = argv[2];

        if (command == "inspect") {
            const auto info = mqt::core::inspectFile(path);
            std::cout
                << "path=" << info.path.string() << "\n"
                << "size=" << info.sizeBytes << "\n"
                << "tier=" << mqt::core::toString(info.tier) << "\n"
                << "utf8_bom=" << (info.hasUtf8Bom ? "true" : "false") << "\n"
                << "newline=" << mqt::core::toString(info.newlineStyle) << "\n";
            return EXIT_SUCCESS;
        }

        if (command == "index") {
            mqt::core::BuildPreviewOptions options;
            if (argc >= 4) {
                options.maxBlocks = static_cast<std::size_t>(parseU64(argv[3]));
            }
            const auto index = mqt::core::buildPreviewIndex(path, options);
            std::cout
                << "blocks=" << index.blocks.size() << "\n"
                << "bytes_scanned=" << index.bytesScanned << "\n"
                << "truncated=" << (index.truncated ? "true" : "false") << "\n";
            for (const auto& block : index.blocks) {
                std::cout
                    << block.id << "\t"
                    << mqt::core::toString(block.type) << "\t"
                    << block.sourceRange.start << "\t"
                    << block.sourceRange.end;
                if (block.type == mqt::core::MarkdownBlockType::Heading) {
                    std::cout << "\t" << static_cast<int>(block.headingLevel) << "\t" << block.text;
                }
                std::cout << "\n";
            }
            return EXIT_SUCCESS;
        }

        if (command == "search") {
            if (argc < 4) {
                printUsage();
                return EXIT_FAILURE;
            }
            mqt::core::SearchOptions options;
            if (argc >= 5) {
                options.maxResults = static_cast<std::size_t>(parseU64(argv[4]));
            }
            const auto result = mqt::core::searchLiteral(path, argv[3], options);
            std::cout
                << "matches=" << result.hits.size() << "\n"
                << "bytes_scanned=" << result.bytesScanned << "\n"
                << "truncated=" << (result.truncated ? "true" : "false") << "\n";
            for (const auto& hit : result.hits) {
                std::cout
                    << hit.position.line << "\t"
                    << hit.position.column << "\t"
                    << hit.sourceRange.start << "\t"
                    << hit.sourceRange.end << "\n";
            }
            return EXIT_SUCCESS;
        }

        if (command == "locate") {
            if (argc < 5) {
                printUsage();
                return EXIT_FAILURE;
            }
            const auto start = parseU64(argv[3]);
            const auto end = parseU64(argv[4]);
            const auto result = mqt::core::locateByteRange(path, {start, end});
            std::cout
                << "start_line=" << result.start.line << "\n"
                << "start_column=" << result.start.column << "\n"
                << "end_line=" << result.end.line << "\n"
                << "end_column=" << result.end.column << "\n";
            return EXIT_SUCCESS;
        }

        if (command == "chunk") {
            if (argc < 5) {
                printUsage();
                return EXIT_FAILURE;
            }
            const auto start = parseU64(argv[3]);
            const auto end = parseU64(argv[4]);
            std::cout << mqt::core::readRange(path, {start, end});
            return EXIT_SUCCESS;
        }

        printUsage();
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
