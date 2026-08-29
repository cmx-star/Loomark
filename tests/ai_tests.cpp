// 批次 13：AI Provider 抽象 / SSE 解析 / 上下文装箱 / 会话 测试
#include "ai/chat_session.h"
#include "ai/ai_provider.h"

#include <iostream>
#include <stdexcept>

namespace {

using namespace mqt::ai;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class FakeTransport final : public AiTransport {
public:
    std::pair<int, QString> post(const Request& request) override
    {
        lastRequest = request;
        return {200,
            "data: {\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n"
            "data: {\"choices\":[{\"delta\":{\"content\":\" world\"}}]}\n"
            "data: [DONE]\n"};
    }
    Request lastRequest;
};

void testSseParsing()
{
    const auto result = parseSseChunk(
        "data: {\"choices\":[{\"delta\":{\"content\":\"A\"}}]}\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"B\"}}]}\n"
        "data: [DONE]\n");
    require(result.deltas.size() == 2 && result.deltas[0] == "A" &&
            result.deltas[1] == "B", "sse deltas parsed");
    require(result.done, "sse done detected");
}

void testProviderAndRedaction()
{
    FakeTransport transport;
    OpenAiCompatibleProvider provider(&transport);
    provider.setApiKey(QStringLiteral("sk-1234567890abcdef"));
    ChatRequest request;
    request.messages.push_back({QStringLiteral("user"), QStringLiteral("hi")});
    QString reply, error;
    require(provider.complete(request, &reply, &error), "provider completes");
    require(reply == "Hello world", "streamed reply accumulated");
    require(transport.lastRequest.apiKey == "sk-1234567890abcdef",
        "api key passed to transport");
    require(redactApiKey("sk-1234567890abcdef").contains("[redacted]"),
        "api key redaction available");

    // 网络错误契约：status=0 → 明确错误
    class DeadTransport final : public AiTransport {
    public:
        std::pair<int, QString> post(const Request&) override { return {0, {}}; }
    } dead;
    OpenAiCompatibleProvider deadProvider(&dead);
    QString err;
    require(!deadProvider.complete(request, &reply, &err), "network error fails");
    require(err.contains("network error"), "network error explains itself");
}

void testContextPacking()
{
    std::vector<ContextPiece> pieces;
    pieces.push_back({"选区", QString(400, 'a')});   // ~108 tokens
    pieces.push_back({"章节:1", QString(400, 'b')});
    pieces.push_back({"文件:big", QString(40000, 'c')}); // 10000 tokens
    const auto packed = packContext(pieces, 250);
    require(packed.pieces.size() == 2, "only pieces within budget packed");
    require(!packed.dropped.empty() && packed.dropped.front() == "文件:big",
        "oversized source dropped with label");
    require(packed.estimatedTokens <= 250, "budget respected");
}

void testChatSessionAndSummary()
{
    FakeTransport transport;
    OpenAiCompatibleProvider provider(&transport);
    ChatSession session(&provider);
    require(session.send("hello"), "first send accepted");
    require(session.lastReply() == "Hello world", "reply accumulated");
    require(session.history().size() == 2, "history has user+assistant");

    // 重试：清掉回复后重发
    session.retry();
    require(session.lastReply() == "Hello world", "retry re-sends last user text");

    // M56 分层摘要计划
    const QString doc = "# A\na\na\n# B\nb\nb\n";
    const auto plan = ChatSession::summarizePlan(doc, 1000);
    require(plan.size() == 2, "two section chunks planned");
    require(plan[0].sectionTitle == "# A", "first chunk titled A");
}

} // namespace

int main()
{
    try {
        testSseParsing();
        testProviderAndRedaction();
        testContextPacking();
        testChatSessionAndSummary();
        std::cout << "ai tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
