// 批次 15：RAG/检索/记忆/Agent 测试
#include "ai/rag.h"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testSemanticChunks()
{
    const std::string content = "# A\nchunk a\n\n# B\nchunk b\n";
    mqt::core::MarkdownBlockIndex index;
    index.build(content, 5);
    const auto chunks = mqt::ai::semanticChunks(index, content, 5);
    require(chunks.size() == 2, "two section chunks");
    require(chunks[0].sectionTitle == "# A" && chunks[1].sectionTitle == "# B",
        "chunk titles from headings");
    require(chunks[0].documentVersion == 5, "version recorded");

    // 局部编辑只使受影响块失效：chunkId 稳定（同内容同 ID）
    mqt::core::MarkdownBlockIndex index2;
    index2.build(content, 6);
    const auto chunks2 = mqt::ai::semanticChunks(index2, content, 6);
    require(chunks[0].chunkId == chunks2[0].chunkId, "chunk id stable");
}

void testEmbeddingQueue()
{
    mqt::ai::EmbeddingQueue queue;
    queue.setEmbeddingFn([](const std::string& text) {
        return std::vector<float>{static_cast<float>(text.size()), 1.0f};
    });
    queue.submit(1, 5, "alpha");
    queue.submit(2, 4, "beta"); // 过期版本
    queue.process(5);
    require(queue.hasResult(1), "current task processed");
    require(!queue.hasResult(2), "stale task dropped");
    queue.cancelAll();
    queue.submit(3, 5, "gamma");
    queue.process(5);
    require(!queue.hasResult(3), "cancelled queue drops tasks");
}

void testMetadataStore()
{
    const auto path = std::filesystem::temp_directory_path() / "mqt_meta.txt";
    std::filesystem::remove(path);
    {
        mqt::ai::ChunkMetadataStore store(path);
        store.setDocumentFingerprint("doc1", 12345);
        store.setIndexVersion("doc1", 7);
        require(store.flush(), "flush ok");
    }
    mqt::ai::ChunkMetadataStore reloaded(path);
    require(reloaded.documentFingerprint("doc1").value_or(0) == 12345,
        "fingerprint persisted");
    require(reloaded.indexVersion("doc1").value_or(0) == 7, "index version persisted");
    std::filesystem::remove(path);
}

void testRetriever()
{
    mqt::ai::Retriever retriever;
    mqt::ai::SemanticChunk a{1, 1, "# A", {0, 10}, "quantum computing basics", 0};
    mqt::ai::SemanticChunk b{2, 1, "# B", {10, 20}, "gardening tips", 0};
    retriever.addChunk(a);
    retriever.addChunk(b);
    const auto results = retriever.query("quantum", 5);
    require(results.size() == 1 && results[0].chunkId == 1,
        "query hits the right chunk");
    require(results[0].sourceRange.start == 0, "source range available for jump");
}

void testMemoryAndAgent()
{
    mqt::ai::MemoryStore memory;
    const auto id = memory.propose("user prefers concise answers", "global");
    require(memory.confirmed("global").empty(), "proposal not active until confirmed");
    require(memory.confirm(id), "confirm proposal");
    require(memory.confirmed("global").size() == 1, "confirmed memory active");
    require(memory.remove(id), "memory removable");

    mqt::ai::AgentToolbox toolbox;
    toolbox.registerReadOnlyTool("read_section",
        [](const std::vector<std::string>& args) { return "section:" + args.at(0); });
    toolbox.registerWriteTool("write_file");
    require(toolbox.invoke("read_section", {"2"}) == "section:2", "read tool works");
    require(toolbox.invoke("write_file", {}).find("denied") != std::string::npos,
        "write tool denied by default");
    require(toolbox.invoke("missing", {}).find("unknown") != std::string::npos,
        "unknown tool reported");
    require(toolbox.auditLog().size() >= 4, "audit log recorded");
}

} // namespace

int main()
{
    try {
        testSemanticChunks();
        testEmbeddingQueue();
        testMetadataStore();
        testRetriever();
        testMemoryAndAgent();
        std::cout << "rag tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
