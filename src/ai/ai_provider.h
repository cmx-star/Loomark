#pragma once

#include <QString>
#include <functional>
#include <string>
#include <vector>

namespace mqt::ai {

/// M53 Provider 抽象：OpenAI 兼容 chat/completions（SSE 流式）。
/// 应用层只依赖本接口与纯数据结构；网络实现可替换（测试用 FakeTransport）。
struct ChatMessage {
    QString role;    // system / user / assistant
    QString content;
};

struct ChatRequest {
    std::vector<ChatMessage> messages;
    QString model = QStringLiteral("gpt-4o-mini");
    double temperature = 0.2;
};

struct ChatEvent {
    enum class Kind { Delta, Done, Error };
    Kind kind = Kind::Delta;
    QString text;     // Delta 增量文本 / Error 消息
};

/// 传输抽象：一次非流式 POST，返回响应体（SSE 文本或 JSON）。
class AiTransport {
public:
    virtual ~AiTransport() = default;
    struct Request {
        QString endpoint;   // https://host/v1/chat/completions
        QString apiKey;
        QString body;       // JSON
        int timeoutMs = 30000;
    };
    /// 返回 HTTP 状态码与响应体；网络错误 status=0。
    virtual std::pair<int, QString> post(const Request& request) = 0;
};

/// M57 隐私：任何日志/错误输出都经脱敏。
[[nodiscard]] QString redactApiKey(const QString& maybeKey);

/// SSE 流式解析（纯函数，可离线测试）：
/// 输入一段 SSE 文本，输出 delta 文本序列与是否结束。
struct SseParseResult {
    std::vector<QString> deltas;
    bool done = false;
    std::vector<QString> errors;
};
[[nodiscard]] SseParseResult parseSseChunk(const QString& data);

/// OpenAI 兼容 Provider：组装请求体并解析 SSE。
class OpenAiCompatibleProvider {
public:
    explicit OpenAiCompatibleProvider(AiTransport* transport);

    void setEndpoint(const QString& endpoint) { endpoint_ = endpoint; }
    void setApiKey(const QString& key) { apiKey_ = key; } // 仅内存保存
    void setModel(const QString& model) { model_ = model; }

    /// 非流式便捷封装：取回完整回复（内部仍解析 SSE 行）。
    /// 返回 false 时 error 带脱敏后的原因。
    bool complete(const ChatRequest& request, QString* reply, QString* error);

private:
    AiTransport* transport_ = nullptr;
    QString endpoint_ = QStringLiteral("https://api.openai.com/v1/chat/completions");
    QString apiKey_;
    QString model_ = QStringLiteral("gpt-4o-mini");
};

/// M55 上下文装箱：预算内组装来源可见的上下文。
struct ContextPiece {
    QString label;      // "选区" / "章节:xxx" / "文件:xxx"
    QString content;
};
struct PackedContext {
    std::vector<ContextPiece> pieces;
    std::size_t estimatedTokens = 0; // 按 bytes/4 估算
    std::vector<QString> dropped;    // 超预算被丢弃的来源
};
[[nodiscard]] PackedContext packContext(const std::vector<ContextPiece>& pieces,
    std::size_t maxTokens);

} // namespace mqt::ai
