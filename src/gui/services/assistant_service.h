#pragma once

#include <QObject>
#include <QString>

#include <cstdint>

class QNetworkAccessManager;

namespace pokedex {

class AiAssistant;

// The config-file key under which the user's AI-assistant API key is stored (see
// storage/workspace.h's key=value config). Named vendor-neutrally on purpose — the
// stored secret is whatever the active provider needs; swapping providers reuses
// the same key. Empty/absent means "no key configured yet".
inline constexpr char kAssistantApiKeyConfigKey[] = "assistant_api_key";

// Optional config key overriding which model the active provider queries (e.g. a
// specific Gemini tier your account can access). Absent = the provider's built-in
// default. Vendor-neutral name: it's whatever string the active provider interprets.
inline constexpr char kAssistantModelConfigKey[] = "assistant_model";

// GUI — the transport half of the AI-assistant module: a QObject that POSTs a text
// prompt to the assistant and emits the answer. It depends only on the Qt-free
// AiAssistant seam for *how* to shape the request and parse the reply, and on the
// key=value config for the API key — it never names the concrete provider. Swapping
// providers happens at the composition root (main.cpp) by constructing a different
// AiAssistant; this class, its callers, and the Settings key field are untouched.
//
// The key is read fresh from config on each ask(), so changing it in Settings takes
// effect immediately with no restart. When no key is configured the call fails fast
// with a message pointing the user at Settings, rather than hitting the network.
//
// One request at a time is the intended usage (the demo dialog disables Send while a
// call is in flight). Because ONE service is shared app-wide, a stale reply must not
// be delivered to whatever caller is connected when it lands: a monotonic generation
// counter drops every reply but the most recent ask()'s. (Otherwise closing the demo
// dialog mid-request and reopening it would show the first question's answer against
// the second question.)
class AssistantService : public QObject {
    Q_OBJECT

public:
    // `assistant` (the vendor-neutral seam) must outlive this service.
    explicit AssistantService(const AiAssistant& assistant, QObject* parent = nullptr);

    // Send `prompt` to the assistant. Eventually emits exactly one of answerReady()
    // or failed(). A blank prompt or a missing API key fails synchronously-ish via
    // failed() without a network call.
    void ask(const QString& prompt);

Q_SIGNALS:
    void answerReady(const QString& text);
    void failed(const QString& message);

private:
    const AiAssistant& assistant_;
    QNetworkAccessManager* nam_;
    // Id of the most recent ask(); a reply whose captured generation differs is stale
    // (a superseded or a closed-dialog request) and is dropped without emitting.
    std::uint64_t generation_ = 0;
};

}  // namespace pokedex
