#pragma once

#include "ai/ai_provider.h"
#include "core/markdown_block.h"

#include <atomic>
#include <filesystem>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mqt::ai {

using mqt::core::ByteRange;

/// M62 语义分块：复用统一块模型，按标题章节分组，chunkId 稳定
/// （FNV(章节路径 + 块起始偏移)），局部编辑只使受影响块失效。
struct SemanticChunk {
    std::uint64_t chunkId = 0;
    std::uint64_t documentVersion = 0;
    std::string sectionTitle;
    ByteRange sourceRange{};
    std::string text;
    std::uint64_t hash = 0; // 内容指纹，增量失效判断
};

[[nodiscard]] std::vector<SemanticChunk> semanticChunks(
    const mqt::core::MarkdownBlockIndex& index, std::string_view content,
    std::uint64_t documentVersion);

/// M63 Embedding 队列：版本化任务、取消、迟到结果丢弃。
/// EmbeddingFn 由上层注入（真实向量化服务或测试假实现）。
class EmbeddingQueue {
public:
    using EmbeddingFn = std::function<std::vector<float>(const std::string&)>;

    void setEmbeddingFn(EmbeddingFn fn) { embeddingFn_ = std::move(fn); }
    void submit(std::uint64_t chunkId, std::uint64_t documentVersion,
        const std::string& text);
    void cancelAll() { cancelled_ = true; }
    /// 处理队列：仅处理 documentVersion == latestVersion 且未取消的任务。
    void process(std::uint64_t latestVersion);
    [[nodiscard]] bool hasResult(std::uint64_t chunkId) const;
    [[nodiscard]] std::vector<float> embeddingOf(std::uint64_t chunkId) const;

private:
    struct Task {
        std::uint64_t chunkId = 0;
        std::uint64_t documentVersion = 0;
        std::string text;
    };
    std::vector<Task> pending_;
    std::map<std::uint64_t, std::vector<float>> results_;
    EmbeddingFn embeddingFn_;
    std::atomic_bool cancelled_{false};
};

/// M64 元数据存储：文档指纹与分块索引版本（文件持久化，D04 SQLite 决策门
/// 之后再切换存储后端；接口不变）。
class ChunkMetadataStore {
public:
    explicit ChunkMetadataStore(std::filesystem::path storePath);
    void setDocumentFingerprint(const std::string& docId, std::uint64_t fingerprint);
    [[nodiscard]] std::optional<std::uint64_t> documentFingerprint(
        const std::string& docId) const;
    void setIndexVersion(const std::string& docId, std::uint64_t version);
    [[nodiscard]] std::optional<std::uint64_t> indexVersion(const std::string& docId) const;
    bool flush() const; // 持久化到文件
private:
    std::filesystem::path storePath_;
    std::map<std::string, std::uint64_t> fingerprints_;
    std::map<std::string, std::uint64_t> indexVersions_;
};

/// M65 检索：关键词评分 + sourceRange 来源跳转。
struct RetrievedChunk {
    std::uint64_t chunkId = 0;
    double score = 0.0;
    ByteRange sourceRange{};
    std::string sectionTitle;
};

class Retriever {
public:
    void addChunk(const SemanticChunk& chunk);
    /// 关键词检索：命中词计数评分，topK。
    [[nodiscard]] std::vector<RetrievedChunk> query(const std::string& keywords,
        std::size_t topK = 5) const;

private:
    std::vector<SemanticChunk> chunks_;
};

/// M66 长期记忆候选：建议 → 确认 → 生效；模型不能静默写入。
class MemoryStore {
public:
    struct Candidate {
        std::uint64_t id = 0;
        std::string content;
        std::string scope; // global / workspace / document
        bool confirmed = false;
    };
    std::uint64_t propose(const std::string& content, const std::string& scope);
    bool confirm(std::uint64_t id);
    bool remove(std::uint64_t id);
    [[nodiscard]] std::vector<Candidate> confirmed(const std::string& scope) const;

private:
    std::vector<Candidate> candidates_;
    std::uint64_t nextId_ = 1;
};

/// M67 只读 Agent：工具注册 + 权限（默认只读）+ 审计日志。
class AgentToolbox {
public:
    using ToolFn = std::function<std::string(const std::vector<std::string>&)>;

    /// 注册只读工具（read-only 默认允许）；写工具注册即拒绝执行。
    void registerReadOnlyTool(const std::string& name, ToolFn fn);
    void registerWriteTool(const std::string& name); // 始终拒绝（写走提案协议）
    /// 调用工具：未授权返回错误并记录审计。
    [[nodiscard]] std::string invoke(const std::string& name,
        const std::vector<std::string>& args);
    [[nodiscard]] const std::vector<std::string>& auditLog() const { return auditLog_; }

private:
    std::map<std::string, ToolFn> readTools_;
    std::vector<std::string> writeTools_;
    std::vector<std::string> auditLog_;
};

} // namespace mqt::ai
