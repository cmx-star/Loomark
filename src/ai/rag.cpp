#include "ai/rag.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace mqt::ai {

std::vector<SemanticChunk> semanticChunks(
    const mqt::core::MarkdownBlockIndex& index, std::string_view content,
    std::uint64_t documentVersion)
{
    std::vector<SemanticChunk> chunks;
    std::string currentTitle;
    std::uint64_t sectionStartOffset = 0;

    auto emitChunk = [&](std::uint64_t start, std::uint64_t end) {
        if (end <= start) {
            return;
        }
        SemanticChunk chunk;
        chunk.documentVersion = documentVersion;
        chunk.sectionTitle = currentTitle;
        chunk.sourceRange = {start, end};
        chunk.text = std::string(content.substr(start, end - start));
        chunk.hash = mqt::core::blockHash(chunk.text);
        std::string idSource = currentTitle + "@" + std::to_string(start);
        chunk.chunkId = mqt::core::blockHash(idSource);
        chunks.push_back(std::move(chunk));
    };

    for (const auto& block : index.blocks()) {
        if (block.kind == mqt::core::BlockKind::Heading) {
            emitChunk(sectionStartOffset, block.sourceRange.start);
            currentTitle = block.payload;
            sectionStartOffset = block.sourceRange.start;
        }
    }
    emitChunk(sectionStartOffset, static_cast<std::uint64_t>(content.size()));
    return chunks;
}

void EmbeddingQueue::submit(std::uint64_t chunkId, std::uint64_t documentVersion,
    const std::string& text)
{
    pending_.push_back({chunkId, documentVersion, text});
}

void EmbeddingQueue::process(std::uint64_t latestVersion)
{
    if (cancelled_.load()) {
        pending_.clear();
        return;
    }
    for (auto& task : pending_) {
        if (task.documentVersion != latestVersion) {
            continue; // 迟到任务丢弃
        }
        if (embeddingFn_ && !results_.contains(task.chunkId)) {
            results_[task.chunkId] = embeddingFn_(task.text);
        }
    }
    pending_.clear();
}

bool EmbeddingQueue::hasResult(std::uint64_t chunkId) const
{
    return results_.contains(chunkId);
}

std::vector<float> EmbeddingQueue::embeddingOf(std::uint64_t chunkId) const
{
    const auto it = results_.find(chunkId);
    return it != results_.end() ? it->second : std::vector<float>{};
}

ChunkMetadataStore::ChunkMetadataStore(std::filesystem::path storePath)
    : storePath_(std::move(storePath))
{
    std::ifstream input(storePath_);
    std::string line;
    while (std::getline(input, line)) {
        const auto sep = line.find('=');
        if (sep == std::string::npos) continue;
        const std::string key = line.substr(0, sep);
        const std::uint64_t value = std::strtoull(line.c_str() + sep + 1, nullptr, 10);
        if (key.rfind("fp:", 0) == 0) {
            fingerprints_[key.substr(3)] = value;
        } else if (key.rfind("ix:", 0) == 0) {
            indexVersions_[key.substr(3)] = value;
        }
    }
}

void ChunkMetadataStore::setDocumentFingerprint(const std::string& docId,
    std::uint64_t fingerprint)
{
    fingerprints_[docId] = fingerprint;
}

std::optional<std::uint64_t> ChunkMetadataStore::documentFingerprint(
    const std::string& docId) const
{
    const auto it = fingerprints_.find(docId);
    return it != fingerprints_.end() ? std::optional{it->second} : std::nullopt;
}

void ChunkMetadataStore::setIndexVersion(const std::string& docId, std::uint64_t version)
{
    indexVersions_[docId] = version;
}

std::optional<std::uint64_t> ChunkMetadataStore::indexVersion(const std::string& docId) const
{
    const auto it = indexVersions_.find(docId);
    return it != indexVersions_.end() ? std::optional{it->second} : std::nullopt;
}

bool ChunkMetadataStore::flush() const
{
    std::ofstream output(storePath_, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    for (const auto& [k, v] : fingerprints_) {
        output << "fp:" << k << "=" << v << "\n";
    }
    for (const auto& [k, v] : indexVersions_) {
        output << "ix:" << k << "=" << v << "\n";
    }
    return true;
}

void Retriever::addChunk(const SemanticChunk& chunk)
{
    chunks_.push_back(chunk);
}

std::vector<RetrievedChunk> Retriever::query(const std::string& keywords,
    std::size_t topK) const
{
    std::vector<std::string> terms;
    std::istringstream stream(keywords);
    std::string term;
    while (stream >> term) {
        terms.push_back(term);
    }
    std::vector<RetrievedChunk> results;
    for (const auto& chunk : chunks_) {
        double score = 0;
        for (const auto& t : terms) {
            std::size_t pos = chunk.text.find(t);
            while (pos != std::string::npos) {
                score += 1.0;
                pos = chunk.text.find(t, pos + t.size());
            }
        }
        if (score > 0) {
            results.push_back({chunk.chunkId, score, chunk.sourceRange,
                chunk.sectionTitle});
        }
    }
    std::sort(results.begin(), results.end(),
        [](const auto& a, const auto& b) { return a.score > b.score; });
    if (results.size() > topK) {
        results.resize(topK);
    }
    return results;
}

std::uint64_t MemoryStore::propose(const std::string& content, const std::string& scope)
{
    candidates_.push_back({nextId_, content, scope, false});
    return nextId_++;
}

bool MemoryStore::confirm(std::uint64_t id)
{
    const auto it = std::find_if(candidates_.begin(), candidates_.end(),
        [&](const Candidate& c) { return c.id == id; });
    if (it == candidates_.end()) {
        return false;
    }
    it->confirmed = true;
    return true;
}

bool MemoryStore::remove(std::uint64_t id)
{
    const auto it = std::remove_if(candidates_.begin(), candidates_.end(),
        [&](const Candidate& c) { return c.id == id; });
    if (it == candidates_.end()) {
        return false;
    }
    candidates_.erase(it, candidates_.end());
    return true;
}

std::vector<MemoryStore::Candidate> MemoryStore::confirmed(
    const std::string& scope) const
{
    std::vector<Candidate> out;
    for (const auto& c : candidates_) {
        if (c.confirmed && c.scope == scope) {
            out.push_back(c);
        }
    }
    return out;
}

void AgentToolbox::registerReadOnlyTool(const std::string& name, ToolFn fn)
{
    readTools_[name] = std::move(fn);
}

void AgentToolbox::registerWriteTool(const std::string& name)
{
    writeTools_.push_back(name);
}

std::string AgentToolbox::invoke(const std::string& name,
    const std::vector<std::string>& args)
{
    auditLog_.push_back("invoke:" + name);
    if (std::find(writeTools_.begin(), writeTools_.end(), name) != writeTools_.end()) {
        auditLog_.push_back("denied-write:" + name);
        return "write tool denied: use the edit proposal protocol";
    }
    const auto it = readTools_.find(name);
    if (it == readTools_.end()) {
        auditLog_.push_back("unknown:" + name);
        return "unknown tool";
    }
    return it->second(args);
}

} // namespace mqt::ai
