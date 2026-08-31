#pragma once

#include "ai/ai_provider.h"

#include <QString>
#include <vector>

namespace mqt::ai {

/// M54 原生对话会话：消息历史、取消、重试、迟到结果丢弃。
/// 无 UI 依赖；渲染层订阅回调。
class ChatSession {
public:
    explicit ChatSession(OpenAiCompatibleProvider* provider);

    /// 发起一次对话。返回 false = 上一次请求仍在进行（不会并发覆盖）。
    bool send(const QString& userText);
    void cancel() { cancelled_ = true; }
    [[nodiscard]] bool inFlight() const { return inFlight_; }
    void retry();

    [[nodiscard]] const std::vector<ChatMessage>& history() const { return history_; }
    /// 最近一次回复（流式累积）。
    [[nodiscard]] const QString& lastReply() const { return lastReply_; }
    [[nodiscard]] const QString& lastError() const { return lastError_; }

    /// M56 分层摘要计划：按章节切分，产出每章摘要请求的消息序列。
    struct SummaryPlanItem {
        QString sectionTitle;
        QString chunk;
    };
    [[nodiscard]] static std::vector<SummaryPlanItem> summarizePlan(
        const QString& document, std::size_t chunkChars);

private:
    OpenAiCompatibleProvider* provider_ = nullptr;
    std::vector<ChatMessage> history_;
    QString lastReply_;
    QString lastError_;
    bool inFlight_ = false;
    bool cancelled_ = false;
    QString lastUserText_;
};

} // namespace mqt::ai
