#pragma once

#include "core/document_backend.h"
#include "core/export.h"

#include <cstdint>
#include <string>

namespace mqt::core {

/// M78 移动共享核心：core 层本就 Qt-free（可在 Android/iOS 编译）。
/// 本模块补充移动端特有的抽象：响应式布局、文件选择器、资源预算。

/// M80 响应式状态与布局。
enum class FormFactor { Phone, Tablet };

[[nodiscard]] inline FormFactor formFactorFor(double widthDp, double heightDp)
{
    // 断点：短边 ≥ 600dp 为平板
    const double shortSide = widthDp < heightDp ? widthDp : heightDp;
    return shortSide >= 600.0 ? FormFactor::Tablet : FormFactor::Phone;
}

enum class MobileLayout { SinglePane, Drawer, TwoPane };
[[nodiscard]] inline MobileLayout layoutFor(FormFactor factor, bool drawerOpen)
{
    if (factor == FormFactor::Tablet) {
        return MobileLayout::TwoPane;
    }
    return drawerOpen ? MobileLayout::Drawer : MobileLayout::SinglePane;
}

/// M79 文件选择器抽象（平台壳实现；core 只依赖接口）。
class FilePicker {
public:
    virtual ~FilePicker() = default;
    /// 返回用户选择的路径；取消返回空。
    [[nodiscard]] virtual std::string pickOpenFile() = 0;
    [[nodiscard]] virtual std::string pickSaveLocation(
        const std::string& suggestedName) = 0;
};

/// 移动资源预算（M86 的数据基础）：AI 上下文与导出上限按档位缩减。
struct MobileBudgets {
    static constexpr std::size_t phoneAiContextTokens = 8000;
    static constexpr std::size_t tabletAiContextTokens = 16000;
    static constexpr std::uint64_t phoneMaxExportBytes = 8ULL << 20;
    static constexpr std::uint64_t tabletMaxExportBytes = 32ULL << 20;
};

/// M82 分段编辑后端：20MB 级文件按页装载（PagedDocumentBackend）。
/// 每页独立缓冲，编辑页内生效，保存时三段拼接（头 + 页 + 尾），
/// 与桌面 F01 窗口化保存同构。
class PagedDocumentBackend : public IDocumentBackend {
public:
    PagedDocumentBackend(std::filesystem::path path, std::uint64_t pageSize);

    DocumentSnapshot snapshot() const override;
    DocumentInfo info() const override;
    std::string read(ByteRange range) const override;
    LocateResult locateLines(ByteRange range) const override;
    SearchOutcome search(const SearchQuery& query,
        const std::atomic_bool* cancelFlag = nullptr) const override;
    ApplyResult apply(std::vector<TextEdit> edits, DocumentVersion baseVersion,
        const std::atomic_bool* cancelFlag = nullptr) override;
    void save(const std::atomic_bool* cancelFlag = nullptr) override;
    void saveAs(const std::filesystem::path& path) override;
    DocumentSnapshot reload() override;

    /// 切换到包含 offset 的页。
    void seekTo(std::uint64_t offset);
    [[nodiscard]] std::uint64_t pageStart() const { return pageStart_; }
    [[nodiscard]] std::uint64_t pageEnd() const { return pageEnd_; }

private:
    void loadPage(std::uint64_t offset);

    std::filesystem::path path_;
    std::uint64_t pageSize_;
    std::string buffer_; // 当前页内容（不含 BOM）
    std::uint64_t pageStart_ = 0;
    std::uint64_t pageEnd_ = 0;
    std::uint64_t originalPageEnd_ = 0; // 装载时的页尾（文件内偏移，供三段拼接）
    std::uint64_t bomOffset_ = 0;
    DocumentInfo info_{};
    DocumentVersion version_ = kInitialDocumentVersion;
    std::uint64_t savedVersion_ = kInitialDocumentVersion;
};

} // namespace mqt::core
