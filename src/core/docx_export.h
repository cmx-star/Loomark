#pragma once

#include "core/export.h"

#include <string>

namespace mqt::core {

/// M50 DOCX 后端：Markdown 块 → OOXML（docx）。
/// 标题→Heading1..3 样式、段落、代码块（等宽）、列表；不支持语义产生
/// 可定位告警（warnings），绝不静默丢失。
class DocxExporter {
public:
    struct Outcome {
        bool ok = false;
        std::string docx;      // 完整 docx 字节流（zip）
        std::vector<std::string> warnings; // 不支持语义的定位告警
    };
    /// 从 Markdown 源生成 docx。
    [[nodiscard]] static Outcome exportDocument(std::string_view markdown);
};

/// M51 导出预设：文档类型 → 导出清单生成。
enum class ExportPreset { Article, Book, Demo, Site };

class PresetRegistry {
public:
    struct Preset {
        std::string name;
        ExportPreset kind = ExportPreset::Article;
        std::vector<std::filesystem::path> sources; // book 站点为多源
        ExportFormat format = ExportFormat::Gfm;
    };
    [[nodiscard]] static std::vector<ExportManifest> manifestsFor(const Preset& preset,
        const std::filesystem::path& outputDir);
};

/// M52 可选外部编译器（Quarkdown/Pandoc/Typst）：默认禁用；
/// detect 探测可用性；run 需显式 enabled + 用户显式调用。
class ExternalCompilerService {
public:
    enum class Kind { Pandoc, Typst, Quarkdown };
    [[nodiscard]] static bool detect(Kind kind); // which 探测
    [[nodiscard]] static bool enabled() { return enabled_; }
    static void setEnabled(bool enabled) { enabled_ = enabled; }
    [[nodiscard]] static bool run(Kind kind, const std::filesystem::path& input,
        const std::filesystem::path& output, std::string* error);
private:
    inline static bool enabled_ = false;
};

} // namespace mqt::core
