#pragma once

#include <QString>
#include <string>

namespace mqt::core {

/// M49 PDF 原生后端（QPdfWriter，无 WebView/外部进程）。
/// 大文档按档位限制（默认 ≤ 8MiB 源文本）。
class PdfExporter {
public:
    /// 生成 PDF。失败时 ok=false 并带错误。字体/分页由 Qt 文本引擎处理。
    static bool exportPdf(const QString& sourceText, const QString& targetPath,
        std::string* error, std::uint64_t maxSourceBytes = 8ULL << 20);
};

} // namespace mqt::core
