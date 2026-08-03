#include "gui/services/network_log.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>

namespace pokedex {

// Info-and-above so the outbound-call lines print by default (a bare
// Q_LOGGING_CATEGORY would leave qCInfo filtered on some Qt builds).
Q_LOGGING_CATEGORY(lcNet, "pokedex.net", QtInfoMsg)

namespace {

// Session-cumulative count per host, shared by every logged verb. GUI-thread only,
// so a plain static is safe — no mutex. It answers "are we hammering some API": the
// running total per host is right there on every line. Also stamps the safe-redirect
// policy so no call site can forget it. Logs ONLY the URL (never headers/body), so a
// secret carried in a header is never written out.
void logCall(const char* method, QNetworkRequest& request) {
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    const QUrl url = request.url();
    const QString host = url.host();

    static QHash<QString, int> callsByHost;
    const int count = ++callsByHost[host];

    qCInfo(lcNet).noquote() << method << url.toString(QUrl::PrettyDecoded)
                            << QStringLiteral("[%1: %2 calls this session]").arg(host).arg(count);
}

}  // namespace

QNetworkReply* loggedGet(QNetworkAccessManager* nam, QNetworkRequest request) {
    logCall("GET", request);
    return nam->get(request);
}

QNetworkReply* loggedPost(QNetworkAccessManager* nam, QNetworkRequest request,
                          const QByteArray& body) {
    logCall("POST", request);
    return nam->post(request, body);
}

}  // namespace pokedex
