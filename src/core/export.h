#pragma once

#include "core/byte_range.h"
#include "core/document_file.h"
#include "core/markdown_block.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace mqt::core {

/// M44 文档配置：Front Matter 提取（未知字段原样保留，绝不改写）。
struct FrontMatter {
    bool present = false;
    std::string rawYaml;        // --- 包围的原文（含未知字段）
    std::uint64_t bodyOffset = 0; // 正文起始字节偏移
};

[[nodiscard]] FrontMatter parseFrontMatter(std::string_view content);

/// M45 导出清单与格式。
enum class ExportFormat { Gfm, Txt, SafeHtml };

struct ExportManifest {
    std::filesystem::path sourcePath;
    std::filesystem::path targetPath;
    ExportFormat format = ExportFormat::Gfm;
    ByteRange sectionRange{};  // 选定章节（start==end==0 表示全文）
    std::uint64_t maxOutputBytes = 512ULL << 20; // 资源预算
};

/// 导出事件：progress(已写字节)。
using ExportProgress = std::function<void(std::uint64_t)>;

/// M46/M47：流式导出——源文件分块读取，目标经 AtomicFileWriter 原子落盘；
/// 取消或失败不覆盖既有目标；不依赖 WebView/外部进程。
class ExportTask {
public:
    explicit ExportTask(ExportManifest manifest);
    /// 执行导出。返回写入字节数；取消返回 -1；失败抛异常。
    std::int64_t run(const std::atomic_bool* cancelFlag = nullptr,
        const ExportProgress& progress = {});

private:
    [[nodiscard]] std::string transformLine(std::string_view line) const;
    [[nodiscard]] std::string renderHtmlChunk(std::string_view chunk) const;

    ExportManifest manifest_;
};

} // namespace mqt::core
