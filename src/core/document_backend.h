#pragma once

#include "core/byte_range.h"
#include "core/file_tier.h"
#include "core/document_file.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mqt::core {

using DocumentVersion = std::uint64_t;
inline constexpr DocumentVersion kInitialDocumentVersion = 1;

struct DocumentInfo {
    FileTier tier = FileTier::Normal;
    std::uint64_t sizeBytes = 0;
    bool hasUtf8Bom = false;
    NewlineStyle newlineStyle = NewlineStyle::None;
    bool newlineStyleKnown = false;
};

struct DocumentSnapshot {
    DocumentVersion version = 0;
    DocumentInfo info{};
};

struct TextEdit {
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    std::string newText;
};

enum class ApplyError { None, StaleVersion, OverlappingEdits, RangeInvalid };

struct ApplyResult {
    ApplyError error = ApplyError::None;
    DocumentVersion newVersion = kInitialDocumentVersion;
};

struct SearchQuery {
    std::string needle;
    SearchOptions options;
};

struct SearchOutcome {
    SearchResult result;
    bool cancelled = false;
};

class IDocumentBackend {
public:
    virtual ~IDocumentBackend() = default;

    virtual DocumentSnapshot snapshot() const = 0;
    virtual DocumentInfo info() const = 0;
    virtual std::string read(ByteRange range) const = 0;
    virtual LocateResult locateLines(ByteRange range) const = 0;
    virtual SearchOutcome search(const SearchQuery& query,
        const std::atomic_bool* cancelFlag = nullptr) const = 0;
    virtual ApplyResult apply(std::vector<TextEdit> edits,
        DocumentVersion baseVersion,
        const std::atomic_bool* cancelFlag = nullptr) = 0;
    virtual void save(const std::atomic_bool* cancelFlag = nullptr) = 0;
    virtual void saveAs(const std::filesystem::path& path) = 0;
    virtual DocumentSnapshot reload() = 0;
};

class FileDocumentBackend : public IDocumentBackend {
public:
    explicit FileDocumentBackend(const std::filesystem::path& path);
    FileDocumentBackend(const FileDocumentBackend&) = delete;
    FileDocumentBackend& operator=(const FileDocumentBackend&) = delete;

    DocumentSnapshot snapshot() const override;
    DocumentInfo info() const override;
    std::string read(ByteRange range) const override;
    LocateResult locateLines(ByteRange range) const override;
    SearchOutcome search(const SearchQuery& query,
        const std::atomic_bool* cancelFlag = nullptr) const override;
    ApplyResult apply(std::vector<TextEdit> edits,
        DocumentVersion baseVersion,
        const std::atomic_bool* cancelFlag = nullptr) override;
    void save(const std::atomic_bool* cancelFlag = nullptr) override;
    void saveAs(const std::filesystem::path& path) override;
    DocumentSnapshot reload() override;

private:
    void loadFromFile();
    void saveToFile();

    std::filesystem::path path_;
    std::string buffer_;
    std::uint64_t bomOffset_ = 0;
    DocumentInfo info_{};
    DocumentVersion version_ = kInitialDocumentVersion;
};

} // namespace mqt::core
