#include "gui/services/assistant_service.h"

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QVariant>

#include <cstdint>
#include <optional>
#include <string>

#include "core/app/ai_assistant.h"

// QByteArray has no fromStdString(); wrap the bytes verbatim (UTF-8 for the JSON
// body, ASCII for the header names/values) without a lossy QString round-trip.
static QByteArray toByteArray(const std::string& s) {
    return QByteArray(s.data(), static_cast<qsizetype>(s.size()));
}

#include "core/storage/workspace.h"
#include "gui/services/network_log.h"

namespace pokedex {

AssistantService::AssistantService(const AiAssistant& assistant, QObject* parent)
    : QObject(parent), assistant_(assistant), nam_(new QNetworkAccessManager(this)) {}

void AssistantService::ask(const QString& prompt) {
    const QString trimmed = prompt.trimmed();
    if (trimmed.isEmpty()) {
        Q_EMIT failed(tr("Please type something to ask."));
        return;
    }

    // The key is read fresh each call so a change in Settings applies with no restart.
    const std::optional<std::string> apiKey = readConfigValue(kAssistantApiKeyConfigKey);
    if (!apiKey) {
        Q_EMIT failed(tr("No AI assistant API key is set. Add one in Settings first."));
        return;
    }

    AiPrompt aiPrompt;
    aiPrompt.userText = trimmed.toStdString();
    const AiRequest req = assistant_.buildRequest(aiPrompt, *apiKey);

    QNetworkRequest request{QUrl(QString::fromStdString(req.url))};
    for (const AiHttpHeader& header : req.headers) {
        request.setRawHeader(toByteArray(header.name), toByteArray(header.value));
    }

    const std::uint64_t generation = ++generation_;
    QNetworkReply* reply = loggedPost(nam_, request, toByteArray(req.body));
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        reply->deleteLater();
        // Drop a stale reply (a newer ask() has superseded this one, or the caller that
        // asked is gone) so its answer can't be shown against a different question.
        if (generation != generation_) {
            return;
        }
        const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int code = status.isValid() ? status.toInt() : 0;
        const bool httpOk = code >= 200 && code < 300;
        const std::string body = reply->readAll().toStdString();

        const AiResult result = assistant_.parseResponse(body, httpOk);
        if (result.ok) {
            Q_EMIT answerReady(QString::fromStdString(result.text));
        } else {
            Q_EMIT failed(QString::fromStdString(result.errorMessage));
        }
    });
}

}  // namespace pokedex
