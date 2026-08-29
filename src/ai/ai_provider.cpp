#include "ai/ai_provider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace mqt::ai {

QString redactApiKey(const QString& maybeKey)
{
    if (maybeKey.size() <= 8) {
        return QStringLiteral("[redacted]");
    }
    return maybeKey.left(4) + QStringLiteral("…[redacted]");
}

SseParseResult parseSseChunk(const QString& data)
{
    SseParseResult result;
    const auto lines = data.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || !trimmed.startsWith(QStringLiteral("data:"))) {
            continue;
        }
        const QString payload = trimmed.mid(5).trimmed();
        if (payload == QStringLiteral("[DONE]")) {
            result.done = true;
            continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8());
        if (!doc.isObject()) {
            continue;
        }
        const QJsonObject obj = doc.object();
        if (obj.contains(QStringLiteral("error"))) {
            result.errors.push_back(QJsonDocument(obj).toJson());
            continue;
        }
        const auto choices = obj.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            const auto delta = choices[0].toObject()
                                   .value(QStringLiteral("delta"))
                                   .toObject();
            const QString text = delta.value(QStringLiteral("content")).toString();
            if (!text.isEmpty()) {
                result.deltas.push_back(text);
            }
        }
    }
    return result;
}

OpenAiCompatibleProvider::OpenAiCompatibleProvider(AiTransport* transport)
    : transport_(transport)
{
}

bool OpenAiCompatibleProvider::complete(const ChatRequest& request,
    QString* reply, QString* error)
{
    if (transport_ == nullptr) {
        if (error != nullptr) *error = QStringLiteral("no transport");
        return false;
    }
    QJsonArray messages;
    for (const auto& m : request.messages) {
        messages.append(QJsonObject{
            {QStringLiteral("role"), m.role},
            {QStringLiteral("content"), m.content},
        });
    }
    const QJsonObject body{
        {QStringLiteral("model"), request.model},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("temperature"), request.temperature},
        {QStringLiteral("stream"), true},
    };
    AiTransport::Request req;
    req.endpoint = endpoint_;
    req.apiKey = apiKey_;
    req.body = QString::fromUtf8(QJsonDocument(body).toJson());
    req.timeoutMs = 30000;

    const auto [status, data] = transport_->post(req);
    if (status == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("network error (timeout or connection)");
        }
        return false;
    }
    if (status != 200) {
        if (error != nullptr) {
            *error = QStringLiteral("HTTP %1: %2")
                         .arg(status)
                         .arg(redactApiKey(data.left(200)));
        }
        return false;
    }

    const auto parsed = parseSseChunk(data);
    if (!parsed.errors.empty()) {
        if (error != nullptr) {
            *error = QStringLiteral("provider error: %1")
                         .arg(redactApiKey(parsed.errors.front().left(200)));
        }
        return false;
    }
    for (const auto& delta : parsed.deltas) {
        if (reply != nullptr) {
            *reply += delta;
        }
    }
    return true;
}

PackedContext packContext(const std::vector<ContextPiece>& pieces,
    std::size_t maxTokens)
{
    PackedContext packed;
    std::size_t budget = maxTokens;
    for (const auto& piece : pieces) {
        const std::size_t cost = piece.content.toUtf8().size() / 4 + 8;
        if (cost <= budget) {
            packed.pieces.push_back(piece);
            packed.estimatedTokens += cost;
            budget -= cost;
        } else {
            packed.dropped.push_back(piece.label);
        }
    }
    return packed;
}

} // namespace mqt::ai
