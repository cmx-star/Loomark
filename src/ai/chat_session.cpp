#include "ai/chat_session.h"

#include <QList>
#include <QStringList>

#include <algorithm>

namespace mqt::ai {

ChatSession::ChatSession(OpenAiCompatibleProvider* provider)
    : provider_(provider)
{
}

bool ChatSession::send(const QString& userText)
{
    if (inFlight_ || provider_ == nullptr) {
        return false;
    }
    cancelled_ = false;
    inFlight_ = true;
    lastUserText_ = userText;
    lastError_.clear();
    lastReply_.clear();

    history_.push_back({QStringLiteral("user"), userText});
    ChatRequest request;
    request.messages = history_;

    // 流式累积（SSE 增量追加到 lastReply_）
    QString accumulated;
    const bool ok = provider_->complete(request, &accumulated, &lastError_);
    if (cancelled_) {
        inFlight_ = false;
        history_.pop_back(); // 取消的请求不进历史
        return true;
    }
    if (ok) {
        lastReply_ = accumulated;
        history_.push_back({QStringLiteral("assistant"), accumulated});
    } else {
        history_.pop_back(); // 失败不进历史（可重试）
    }
    inFlight_ = false;
    return true;
}

void ChatSession::retry()
{
    if (inFlight_ || lastUserText_.isEmpty()) {
        return;
    }
    send(lastUserText_);
}

std::vector<ChatSession::SummaryPlanItem> ChatSession::summarizePlan(
    const QString& document, std::size_t chunkChars)
{
    std::vector<SummaryPlanItem> plan;
    const QStringList lines = document.split(QLatin1Char('\n'));
    QString currentTitle;
    QString currentChunk;
    auto flush = [&] {
        if (!currentChunk.trimmed().isEmpty()) {
            plan.push_back({currentTitle.isEmpty() ? QStringLiteral("(开头)")
                                                   : currentTitle,
                currentChunk});
            currentChunk.clear();
        }
    };
    for (const QString& line : lines) {
        if (line.startsWith(QStringLiteral("#"))) {
            flush();
            currentTitle = line.trimmed();
        }
        currentChunk += line + QLatin1Char('\n');
        if (static_cast<std::size_t>(currentChunk.size()) >= chunkChars) {
            flush();
        }
    }
    flush();
    return plan;
}

} // namespace mqt::ai
