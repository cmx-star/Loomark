#include "core/export.h"

#include <fstream>
#include <stdexcept>

namespace mqt::core {

FrontMatter parseFrontMatter(std::string_view content)
{
    FrontMatter fm;
    if (content.rfind("---\n", 0) != 0 && content.rfind("---\r\n", 0) != 0) {
        return fm;
    }
    const auto close = content.find("\n---", 4);
    if (close == std::string_view::npos) {
        return fm;
    }
    fm.present = true;
    // rawYaml 不含首行 "---\n"
    const auto yamlStart = 4;
    fm.rawYaml = std::string(content.substr(yamlStart, close - yamlStart));
    auto bodyStart = content.find('\n', close + 1);
    fm.bodyOffset = bodyStart == std::string_view::npos ? content.size() : bodyStart + 1;
    return fm;
}

ExportTask::ExportTask(ExportManifest manifest)
    : manifest_(std::move(manifest))
{
}

std::string ExportTask::transformLine(std::string_view line) const
{
    if (manifest_.format != ExportFormat::Txt) {
        return std::string(line);
    }
    // TXT：剥离标题 # 前缀与围栏标记，保留正文
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    std::size_t hashes = 0;
    while (i + hashes < line.size() && line[i + hashes] == '#') {
        ++hashes;
    }
    if (hashes > 0 && i + hashes < line.size() && line[i + hashes] == ' ') {
        return std::string(line.substr(i + hashes + 1));
    }
    if (line.compare(i, 3, "```") == 0 || line.compare(i, 3, "~~~") == 0) {
        return {};
    }
    return std::string(line);
}

std::string ExportTask::renderHtmlChunk(std::string_view chunk) const
{
    // 安全 HTML：整体转义正文中的 <>&，禁止脚本/远程资源执行。
    // （块级结构化渲染在导出中心批次增强；本层保证安全性。）
    std::string out;
    out.reserve(chunk.size() + 64);
    for (char c : chunk) {
        switch (c) {
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '&': out += "&amp;"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::int64_t ExportTask::run(const std::atomic_bool* cancelFlag,
    const ExportProgress& progress)
{
    std::error_code ec;
    if (!std::filesystem::exists(manifest_.sourcePath, ec) || ec) {
        throw std::runtime_error("source file does not exist");
    }
    const auto total = std::filesystem::file_size(manifest_.sourcePath, ec);
    if (ec) {
        throw std::runtime_error("failed to stat source");
    }

    // 选定章节范围
    const bool wholeDoc = manifest_.sectionRange.start == 0 &&
        manifest_.sectionRange.end == 0;
    const std::uint64_t rangeStart = wholeDoc ? 0 : manifest_.sectionRange.start;
    const std::uint64_t rangeEnd = wholeDoc ? total : std::min<std::uint64_t>(manifest_.sectionRange.end, total);
    if (rangeStart > rangeEnd) {
        throw std::runtime_error("invalid section range");
    }
    const std::uint64_t span = rangeEnd - rangeStart;
    if (span > manifest_.maxOutputBytes) {
        throw std::runtime_error("export exceeds resource budget");
    }

    std::ifstream input(manifest_.sourcePath, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open source");
    }
    input.seekg(static_cast<std::streamoff>(rangeStart));

    mqt::core::AtomicFileWriter writer(manifest_.targetPath);
    const bool isHtml = manifest_.format == ExportFormat::SafeHtml;
    if (isHtml) {
        writer.write("<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">"
                     "</head><body>\n");
    }

    FrontMatter fm;
    std::uint64_t written = 0;
    std::string chunk;
    constexpr std::uint64_t kChunk = 256ULL * 1024ULL;
    bool first = true;
    while (written < span) {
        if (cancelFlag != nullptr && cancelFlag->load(std::memory_order_relaxed)) {
            return -1; // AtomicFileWriter 析构清理临时文件，目标不受影响
        }
        const auto toRead = std::min<std::uint64_t>(kChunk, span - written);
        chunk.resize(static_cast<std::size_t>(toRead));
        input.read(chunk.data(), static_cast<std::streamsize>(toRead));
        const auto got = static_cast<std::size_t>(input.gcount());
        if (got == 0) {
            break;
        }
        std::string_view data(chunk.data(), got);
        if (first) {
            first = false;
            fm = parseFrontMatter(data);
            if (fm.present && rangeStart == 0) {
                // Front Matter 不导出为正文（配置层内容）
                data.remove_prefix(fm.bodyOffset);
            }
        }
        // 按行转换（GFM 原样 / TXT 剥离标记 / HTML 转义）
        std::string transformed;
        transformed.reserve(data.size());
        std::size_t pos = 0;
        while (pos < data.size()) {
            const auto nl = data.find('\n', pos);
            const auto end = nl == std::string_view::npos ? data.size() : nl;
            std::string_view line = data.substr(pos, end - pos);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            transformed += transformLine(line);
            transformed += '\n';
            if (nl == std::string_view::npos) {
                break;
            }
            pos = nl + 1;
        }
        if (isHtml) {
            transformed = renderHtmlChunk(transformed);
        }
        writer.write(transformed);
        written += got;
        if (progress) {
            progress(written);
        }
    }

    if (isHtml) {
        writer.write("</body></html>\n");
    }
    writer.commit();
    return static_cast<std::int64_t>(written);
}

} // namespace mqt::core
